SET (CODES
web/bootstrap/doxy-boot.js
web/bootstrap/footer.html
web/bootstrap/header.html
web/bootstrap/customdoxygen.css
offcanvas.css
offcanvas.js
solarized-light.css
theme.css
yap.css
bootstrap.min.css
bootstrap.min.js
font-awesome.min.css
jquery-2.0.3.min.js
application.js
pygments.css
misc/icons/yap_64x64x32.png 
misc/icons/yap_256x256x32.png
misc/icons/yap_128x128x32.png
misc/icons/yap_48x48x32.png
  )

SET (DOCS
builtins.md
chr.md
clpqr.md
download.md
extensions.md
fli.md
install.md
packages.md
run.md
swi.md
syntax.md
yap.md
  )
  
  

# add a target to generate API documentation with Doxygen

find_package(Doxygen)
option(WITH_DOCUMENTATION "Create and install the HTML based API documentation (requires Doxygen)" ${DOXYGEN_FOUND})

if(WITH_DOCUMENTATION)
    if(NOT DOXYGEN_FOUND)
        message(FATAL_ERROR "Doxygen is needed to build the documentation.")
    endif()

    set(doxyfile_in ${CMAKE_CURRENT_SOURCE_DIR}/Doxyfile.in)
    set(doxyfile ${CMAKE_CURRENT_BINARY_DIR}/Doxyfile)

    
   configure_file(${doxyfile_in} ${doxyfile} @ONLY)

     add_custom_target(doc
        COMMAND ${DOXYGEN_EXECUTABLE} ${doxyfile}
        WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
        COMMENT "Generating API documentation with Doxygen"
        VERBATIM)

    install(DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/html DESTINATION ${docdir})
    install(FILES  ${CODES} DESTINATION ${docdir})

  endif()
