"""Test help output of various yap_ipython entry points"""

# Copyright (c) yap_ipython Development Team.
# Distributed under the terms of the Modified BSD License.

import yap_ipython.testing.tools as tt


def test_ipython_help():
    tt.help_all_output_test()

def test_profile_help():
    tt.help_all_output_test("profile")

def test_profile_list_help():
    tt.help_all_output_test("profile list")

def test_profile_create_help():
    tt.help_all_output_test("profile create")

def test_locate_help():
    tt.help_all_output_test("locate")

def test_locate_profile_help():
    tt.help_all_output_test("locate profile")

def test_trust_help():
    tt.help_all_output_test("trust")
