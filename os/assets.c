/*************************************************************************
 *									 *
 *	 YAP Prolog 							 *
 *									 *
 *	Yap Prolog was developed at NCCUP - Universidade do Porto	 *
 *									 *
 * Copyright L.Damas, V.S.Costa and Universidade do Porto 1985-1997	 *
 *									 *
 **************************************************************************
 *									 *
 * File:		assets.c						 *
 * Last rev:	5/2/88							 *
 * mods:									 *
 * comments:	Asset Support in ANDROID			 *
 *									 *
 *************************************************************************/
#ifdef SCCSA
static char SccsId[] = "%W% %G%";
#endif

/**
 * @file   assets.c
 * @author VITOR SANTOS COSTA <vsc@VITORs-MBP.lan>
 * @date   Thu Nov 19 10:53:20 2015
 *
 * @brief  File Aliases
 *
 *
 */

#include <stdbool.h>
#include "sysbits.h"
// for native asset manager
#include <sys/types.h>


#if __ANDROID__

#include <android/asset_manager.h>
#include <android/native_activity.h>

AAssetManager *Yap_assetManager;

jboolean
Java_pt_up_yap_app_YAPDroid_setAssetManager(JNIEnv *env, jclass clazz, jobject assetManager) {
    Yap_assetManager = AAssetManager_fromJava(env, assetManager);
    return true;
}

static void *
open_asset__(VFS_t *me, int sno, const char *fname, const char *io_mode) {
    int mode;
    const void *buf;
    if (strchr(io_mode, 'B'))
        mode = AASSET_MODE_BUFFER;
    else {
        mode = AASSET_MODE_UNKNOWN;
    }
    fname += strlen(me->prefix) + 1;
    AAsset *a = AAssetManager_open(Yap_assetManager, fname, mode);
    if (!a)
        return NULL;
    // try not to use it as an asset
    off64_t sz = AAsset_getLength64(a), sz0 = 0;
    int fd;
    StreamDesc *st = GLOBAL_Stream + sno;
    if ((buf = AAsset_getBuffer(a))) {
        // copy to memory
        bool rc = Yap_set_stream_to_buf(st, buf, sz);
        if (rc) AAsset_close(a);
        st->vfs = NULL;
        st->vfs_handle = NULL;
        st->status = InMemory_Stream_f|Seekable_Stream_f|Input_Stream_f;
         return st;
    } else if ((fd = AAsset_openFileDescriptor64(a, &sz0, &sz)) >= 0) {
        // can use it as read-only file
        st->file = fdopen(fd, "r");
        st->vfs = NULL;
        st->vfs_handle = NULL;
        st->status = Seekable_Stream_f|Input_Stream_f;
        return st;
    } else {
        // should be done, but if not
        GLOBAL_Stream[sno].vfs_handle = a;
        st->vfs = me;
        st->status = Input_Stream_f;
        return a;
    }
}

static bool
close_asset(int sno) {
    AAsset_close(GLOBAL_Stream[sno].vfs_handle);
    return true;
}

static int64_t seek64(int sno, int64_t offset, int whence) {
    return AAsset_seek64(GLOBAL_Stream[sno].vfs_handle, offset, whence);
}

static int getc_asset(int sno) {
    int ch;
    if (AAsset_read(GLOBAL_Stream[sno].vfs_handle, &ch, 1))
        return ch;
    return -1;
}


static void *opendir_a(VFS_t *me, const char *dirName) {
    dirName += strlen(me->prefix) + 1;
    return (void *) AAssetManager_openDir(Yap_assetManager, dirName);
}

static const char *readdir_a(void *dirHandle) {
    return AAssetDir_getNextFileName((AAssetDir *) dirHandle);
}

static bool closedir_a(void *dirHandle) {
    AAssetDir_close((AAssetDir *) dirHandle);
    return true;
}


static bool stat_a(VFS_t *me, const char *fname, vfs_stat *out) {
    struct stat bf;
    fname += strlen(me->prefix) + 1;
    if (stat("/assets", &bf)) {

        out->st_dev = bf.st_dev;
        out->st_uid = bf.st_uid;
        out->st_gid = bf.st_gid;
        memcpy(&out->st_atimespec, (const void *) &out->st_atimespec, sizeof(struct timespec));
        memcpy(&out->st_mtimespec, (const void *) &out->st_mtimespec, sizeof(struct timespec));
        memcpy(&out->st_ctimespec, (const void *) &out->st_ctimespec, sizeof(struct timespec));
        memcpy(&out->st_birthtimespec, (const void *) &out->st_birthtimespec,
               sizeof(struct timespec));
    }
    AAsset *a = AAssetManager_open(Yap_assetManager, fname, AASSET_MODE_UNKNOWN);
    // try not to use it as an asset
    out->st_size = AAsset_getLength64(a);
    AAsset_close(a);
    return true;

}

static
bool is_dir_a(VFS_t *me, const char *dirName) {
    bool rc;
    dirName += strlen(me->prefix) + 1;
    // try not to use it as an asset
    AAssetDir *d = AAssetManager_openDir(Yap_assetManager, dirName);
    if (d == NULL)
        return false;
    rc = (AAssetDir_getNextFileName(d) != NULL);
    __android_log_print(ANDROID_LOG_INFO, "YAPDroid", "isdir %s <%p>", dirName, d);
    AAssetDir_close(d);
    return rc;
}

static
bool exists_a(VFS_t *me, const char *dirName) {
    dirName += strlen(me->prefix) + 1;
    // try not to use it as an asset
    AAsset *d = AAssetManager_open(Yap_assetManager, dirName, AASSET_MODE_UNKNOWN);
    __android_log_print(ANDROID_LOG_INFO, "YAPDroid", "exists %s <%p>", dirName, d);
    if (d == NULL)
        return false;
    AAsset_close(d);
    return true;
}


extern char virtual_cwd[YAP_FILENAME_MAX + 1];

static bool set_cwd(VFS_t *me, const char *dirName) {

    chdir("/assets");
    strcpy(virtual_cwd, dirName);
     __android_log_print(ANDROID_LOG_INFO, "YAPDroid",
                        "chdir %s", virtual_cwd);
    Yap_do_low_level_trace =    true;
   return true;
}

#endif


VFS_t *
Yap_InitAssetManager(void) {

#if __ANDROID__
    VFS_t *me;

    /* init standard VFS */
    me = (VFS_t *) Yap_AllocCodeSpace(sizeof(struct vfs));
    me->name = "/assets";
    me->vflags = VFS_CAN_EXEC | VFS_CAN_SEEK |
                 VFS_HAS_PREFIX;  /// the main flags describing the operation of the Fs.
    me->prefix = "/assets";
    /** operations */
    me->open = open_asset__; /// open an object in this space
    me->close = close_asset;         /// close the object
    me->get_char = getc_asset;          /// get an octet to the stream
    me->put_char = NULL;  /// output an octet to the stream
    me->seek = seek64;  /// jump around the stream
    me->opendir = opendir_a; /// open a directory object, if one exists
    me->nextdir = readdir_a; /// open a directory object, if one exists
    me->closedir = closedir_a;            /// close access a directory object
    me->stat = stat_a;            /// obtain size, age, permissions of a file.
    me->isdir = is_dir_a;            /// obtain size, age, permissions of a file.
    me->exists = exists_a;            /// obtain size, age, permissions of a file.
    me->chdir = set_cwd;            /// chnage working directory.
    me->enc = ENC_ISO_UTF8;            /// how the file is encoded.
    me->parsers = NULL;                    /// a set of parsers that can read the stream and generate a term
    me->writers = NULL;
    LOCK(BGL);
    me->next = GLOBAL_VFS;
    GLOBAL_VFS = me;
    return me;
    UNLOCK(BGL);
    return me;
#else
    return NULL;
#endif
}


