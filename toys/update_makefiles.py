#! /usr/bin/python

# Copy to mediaengine/mk and run.
# Removed source files will be removed from the makefiles.
# Added source files will be added to the end of the relevant makefile but
# commented out, when a build fails due to missing file, you can copy/paste 
# the file to the relevant place.
# Definitely still WIP.

import os
import re
import time

ignore_files = '.*test.*'


def read_mk_list(mk_file):
	file_list = []
	f = open(mk_file, 'r')
	for line in f.readlines():
		m = re.match('\s*#?\s*(.*)\.(cc?|cpp|mm?)\s*', line)
		if m and m.group(1):
			file_list.append(m.group(1) + '.' + m.group(2))
	return file_list		

def read_file_list(path, base):
	file_list = []
	for root,_,filenames in os.walk(path):
		for file in filenames:
			ext = os.path.splitext(file)[-1]
			if re.match('.(cc?|cpp|mm?)', ext) and not re.match(ignore_files, file):
				rpath = os.path.relpath(root, base)
				file_list.append(os.path.join(rpath,file))

	return file_list

def write_new_mk_file(mk_file, old_files, new_files):
	file_list = []
	f = open(mk_file, 'r')
	lines = f.readlines()
	f.close()
	
	f = open(mk_file, 'w')
	
	for line in lines:
		remove = False
		for file in old_files:
			if file in line:
				remove = True
				print 'Removing file ' + file + ' from ' + mk_file
				break
		if not remove:
			f.write(line)
	
	if len(new_files) > 0:
		f.write('\n# Files added ' + time.strftime("%Y-%m-%d %H:%M:%S") + '\n')
		for file in new_files:
			print 'Adding file ' + file + ' to ' + mk_file
			f.write('#\t' + file + ' \\\n');
	
	f.close()

def update_mk_file(mk_file, path, base):
	mk_list = read_mk_list(mk_file)
	file_list = read_file_list(path, base)

	old_files = [x for x in mk_list if not x in file_list];
	new_files = [x for x in file_list if not x in mk_list and not re.match('.*unittest.*', x)];

	write_new_mk_file(mk_file, old_files, new_files)


for mk_file in os.listdir('.'):
	if mk_file.endswith('.mk'):
		path = '../' + mk_file.replace('.mk', '').replace('.', '/')
		update_mk_file(mk_file, path, '..')

