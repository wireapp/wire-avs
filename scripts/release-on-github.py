#! /usr/bin/env python3

import github
import os
import re
import subprocess
import sys

def usage():
    print('usage: {} <repo> <path> <version> <message>'.format(sys.argv[0]))
    exit(1)

if len(sys.argv) < 5:
    usage()

repository_name = sys.argv[1]
assets_directory_path = sys.argv[2]
version = sys.argv[3]
message = sys.argv[4]

user = os.getenv('GITHUB_USER', 'wireapp')
token = os.environ.get('GITHUB_TOKEN')
gh = github.Github(token)
grepo = gh.get_user(user).get_repo(repository_name)

releases = grepo.get_releases()
release = None
for r in releases:
    if r.tag_name == version:
        release = r
        break

tag = version
name = version
description = message

if release is None:
    release = grepo.create_git_release(tag, name, description)
else:
    print('[INFO] Release: {} already exists, aborting'.format(name))

for folder_entry in os.listdir(assets_directory_path):
    print('Uploading {} as asset to release {}'.format(folder_entry, name))
    asset_path = os.path.join(assets_directory_path, folder_entry)
    release.upload_asset(asset_path)

