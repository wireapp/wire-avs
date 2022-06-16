#! /usr/bin/env python3

import github
import os
import re
import shutil
import subprocess
import sys

user = 'wireapp'
repo = 'avs-ios-binaries'

def usage():
	print('usage: {} <file> <version>'.format(sys.argv[0]))
	exit()

if len(sys.argv) != 4:
	usage()

fname = sys.argv[1]

if not os.path.exists(fname):
        print('file: {} not found'.format(fname))
        exit()

rb = sys.argv[2]
dest = sys.argv[3]
token = os.environ.get('GITHUB_TOKEN')

if dest == 'appstore':
	repo += '-appstore'

print('repo: {}/{}'.format(user, repo))
print('build: {}'.format(rb))

gh = github.Github(token)
guser = gh.get_user(user)
grepo = guser.get_repo(repo)

grel = grepo.get_releases()

found_rel = None
for r in grel:
	if r.tag_name == rb:
		found_rel = r
		break

if found_rel:
	print('release {} found, aborting'.format(rb))
	exit()

print('release {} not found, cloning'.format(rb))
if os.path.exists(repo):
	shutil.rmtree(repo)
subprocess.call(['git', 'clone', 'https://{}@github.com/{}/{}'.format(token, user, repo)])
cwd = os.getcwd()
os.chdir(repo)
open('releases.txt', 'w').write(rb)
print('tagging and pushing')
subprocess.call(['git', 'commit', '-m', 'version {}'.format(rb), 'releases.txt'])
subprocess.call(['git', 'tag', rb])
subprocess.call(['git', 'push'])
os.chdir(cwd)
found_rel = grepo.create_git_release(rb, rb, 'Version {}'.format(rb))

print('uploading {} as asset to release {}'.format(fname, rb))
found_rel.upload_asset(fname)

