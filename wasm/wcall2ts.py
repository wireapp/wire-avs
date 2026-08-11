#!/usr/bin/env python3
import os
import re
import sys

callbacks = {}

class Write_Helper:
	def __init__(self, file):
		self.file = file
		self.cind = 0
		self.ind = 2
	
	def indent_diff(self, l):
		i = 0
		d = [0, 0]
		s = 0
		for c in l:
			if c == '(' or c == '{' or c == '[':
				i += 1
				d[1] += 1
			elif c == ')' or c == '}' or c == ']':
				i += 1
				if d[1] > 0:
					d[1] -= 1
				else:
					d[0] -= 1
		return d

	def write_blk(self, blk):
		if blk:
			lines = blk.split('\n')
		for l in lines:
			self.write_ln(l)

	def write_ln(self, l):
		blnk = '                                               '
		l = l.strip().rstrip()
		d = self.indent_diff(l)
		self.cind += d[0]
		self.file.write('{}{}\n'.format(blnk[:self.cind * self.ind], l))
		self.cind += d[1]

def c2jstype(ty):
	if ty == 'int' or ty == 'WUSER_HANDLE' or ty == 'size_t' or \
	   ty == 'int8_t' or ty == 'int16_t' or ty == 'int32_t' or \
	   ty == 'uint8_t' or ty == 'uint16_t' or ty == 'uint32_t' or \
	   ty == 'void*':
		return 'number'
	elif ty == 'char*' or ty == 'uint8_t*':
		return 'string'
	elif ty == 'void':
		return 'null'
	#elif ty[-1] == '*':
	#	return 'number'

	return ty

def c2jstype_cb(ty):
	if ty == 'int' or ty == 'WUSER_HANDLE' or \
	   ty == 'int8_t' or ty == 'int16_t' or ty == 'int32_t' or \
	   ty == 'uint8_t' or ty == 'uint16_t' or ty == 'uint32_t' or \
	   ty == 'void*':
		return 'i'
	elif ty == 'size_t':
		return 'i'
	elif ty == 'char*' or ty == 'uint8_t*':
		return 's'
	elif ty == 'void':
		return 'v'
	return ty

def convert_typestring(ty):
	return ty.replace('s', 'i')

def c2tstype(ty):
	if ty == 'int' or ty == 'WUSER_HANDLE' or ty == 'size_t' or \
	   ty == 'int8_t' or ty == 'int16_t' or ty == 'int32_t' or \
	   ty == 'uint8_t' or ty == 'uint16_t' or ty == 'uint32_t' or \
	   ty == 'void*':
		return 'number'
	elif ty == 'char*' or ty == 'uint8_t*':
		return 'string'
	elif ty == 'void':
		return 'void'
	elif ty in callbacks:
		return callbacks[ty]['tsname']
	elif ty[-1] == '*':
		return 'any'
	return ty

# Helper function to convert js/ts variables to kt
def jsts2kt(jsts):
	if jsts == 'int' or jsts == 'WUSER_HANDLE' or jsts == 'size_t' or \
	   jsts == 'int8_t' or jsts == 'int16_t' or jsts == 'int32_t' or \
	   jsts == 'uint8_t' or jsts == 'uint16_t' or jsts == 'uint32_t' or \
	   jsts == 'void*' or jsts == 'number':
		return 'Int'
	elif jsts == 'void':
		return 'Unit'
	elif jsts == 'char*' or jsts == 'uint8_t*' or jsts == 'string':
		return 'String'
	elif jsts == 'string | null':
		return 'String?'
	elif jsts == 'any':
		return 'JsAny?'
	elif jsts in callbacks:
		return callbacks[jsts]['ktsign']
	return 'JsAny?'

def convert_to_camel(fname, istype=False):
	camel = ''
	is_upper = istype
	for c in fname:
		if c == '_':
			is_upper = True
		elif is_upper:
			camel += c.upper()
			is_upper = False
		else:
			camel += c.lower()
	return camel

def parse_param(param, cb=False):
	parts = param.split(' ')
	if len(parts) < 2:
		return None

	if parts[0] == 'const':
		parts = parts[1:]

	ptype = parts[0]
	pname = parts[1]
	if pname[0] == '*':
		ptype = '{}*'.format(ptype)
		pname = pname[1:]
	if pname[-2:] == '[]':
		pname = pname[:-2]

	if pname == 'transient':
		pname = 'trans'

	tsType = c2tstype(ptype)
	if len(parts) > 2 and parts[2] == '__optional':
		tsType += ' | null'
	if len(parts) > 2 and parts[2] == '__any':
		tsType = 'any'

	if cb:
		return (pname, c2jstype_cb(ptype), tsType)
	else:
		return (pname, c2jstype(ptype), tsType)

def should_include_return_type(type):
	return type != 'void'

def convert_fn(fn):

	ignored = ['wcall_setup',
		   'wcall_handle_frame',
		   'wcall_get_members',
		   'wcall_free_members',
		   'wcall_thread_main',
		   'wcall_netprobe',
		   'wcall_debug',
		   'wcall_stats',
		   'wcall_dce_send',
		   'wcall_set_media_laddr',
		   'wcall_run']

	m = re.search(r'(\w+)\s+(\w+)\((.*)\);', fn)
	if m:
		ret = m.group(1)
		fname = m.group(2)
		args = m.group(3)
		args = re.sub(r'\s+\/\*.*?\*\/', '', args)
		args = args.split(', ')

		if fname in ignored:
			return '', ''

		argtypes = []
		argnames = []
		for a in args:
			ainfo = parse_param(a)
			if ainfo:
				argnames.append(ainfo[0])
				argtypes.append(ainfo[1])

		fndef = '{}('.format(convert_to_camel(fname.replace('wcall_', '')))
		kfndef = 'fun {}('.format(convert_to_camel(fname.replace('wcall_', '')))
		i = 0
		for a in argnames:
			if i > 0:
				fndef += ','
				kfndef += ','
			fndef += '\n{}: {}'.format(a, c2tstype(argtypes[i]))
			kfndef += '\n{}: {}'.format(a, jsts2kt(argtypes[i]))
			if a == 'arg':
				fndef += ' = 0'
			i += 1
		if i > 0:
			fndef += '\n'
			kfndef += '\n'
		fndef += '): {} {{\n'.format(c2tstype(ret))

		# kt will need non void return type
		if (should_include_return_type(ret)):
			kfndef += '): {}\n'.format(jsts2kt(ret))
		else:
			kfndef += ')\n'


		if fname == 'wcall_init':
			fndef += 'avs_pc.init(this.em_module, logHandler);\n'
			fndef += 'this.em_module.ccall("wcall_setup", "number", [], []);\n\n'

		for i in range(0, len(argtypes)):
			if argtypes[i] in callbacks:
				nm = argnames[i]
				cb = callbacks[argtypes[i]]
				fn = wrap_function(nm, cb, argtypes[i])
				fndef += '\nconst fn_{} = this.em_module.addFunction('.format(nm)
				fndef += '{}, '.format(fn)
				fndef += '\'{}\');\n'.format(convert_typestring(cb['proto']))
				argnames[i] = 'fn_{}'.format(argnames[i])
				argtypes[i] = 'number'

		if ret == 'void':
			fndef += ''
		else:
			fndef += 'return '

		if fname == 'wcall_set_log_handler':
			fndef += 'logFn = logh;\n'

		fndef += 'this.em_module.ccall(\n'
		fndef += '\'{}\',\n'.format(fname)
		fndef += '\'{}\',\n'.format(c2jstype(ret))

		fndef += '['
		i = 0
		if len(argtypes) > 4:
			fndef += '\n'
			splt = ',\n'
		else:
			splt = ', '

		for a in argtypes:
			if i != 0:
				fndef += splt
			i += 1
			fndef += '\'{}\''.format(a)
		if len(argtypes) > 4:
			fndef += '\n'
		fndef += '],\n'

		fndef += '['
		i = 0
		for a in argnames:
			if i > 0:
				fndef += ','
			fndef += '\n'
			i += 1
			fndef += a
		if i > 0:
			fndef += '\n'
		fndef += ']\n);'
		fndef += '\n}\n\n'

		return fndef, kfndef

	return '', ''

def convert_cb(fn):

	ignore = ['wcall_render_frame_h']
	m = re.search(r'typedef\s+(\w+)\s+\((\w+)\)\((.*)\);', fn)
	if m and m.group(2) not in ignore:
		ret = m.group(1)
		fname = m.group(2)
		args = m.group(3)
		args = re.sub(r'\s+\/\*.*?\*\/', '', args)
		args = args.split(', ')
		argnames = []

		tsname = convert_to_camel(fname, True) + 'andler'
		fndef = 'export type {} = (\n'.format(tsname)
		ktsign = '(('

		fproto = c2jstype_cb(ret)
		first = True
		for a in args:
			ainfo = parse_param(a, cb=True)
			if ainfo:
				argnames.append(ainfo[0])
				fproto += ainfo[1]
				if first:
					first = False
				else:
					fndef += ',\n'
					ktsign += ', '
				fndef += '{}: {}'.format(ainfo[0], ainfo[2])
				ktsign += '{}: {}'.format(ainfo[0], jsts2kt(ainfo[2]))

		ktsign += ') -> {})?'.format(jsts2kt(c2tstype(ret)))

		cb = {
			'name': fname,
			'tsname': tsname,
			'ktsign': ktsign,
			'args': argnames,
			'proto': fproto
		}
		callbacks['{}*'.format(fname)] = cb

		fndef += '\n) => {};\n\n'.format(c2tstype(ret))

		return fndef
	return ''

def wrap_function(fn, cb, ty):
	proto = cb['proto']
	args = cb['args']

	if len(args) > 4:
		fndef = '\n('
	else:
		fndef = '('
	if len(args) > 0:
		fndef += args[0]

	fndef += ': any'

	for a in args[1:]:
		fndef += ', ' + a + ': any'
	fndef += ') => {\n'

	fndef += 'if ({}) {{\n'.format(fn)

	if proto[0] != 'v':
		fndef += 'return '

	fndef += '{}('.format(fn)

	i = 0
	for a in args:
		if i > 0:
			fndef += ','
		fndef += '\n'
		if proto[i + 1] == 's':
			fndef += '{} == 0 ? null : this.em_module.UTF8ToString({})'.format(a, a)
		else:
			fndef += a
		i += 1

	fndef += '\n);\n}'

	if proto[0] != 'v':
		fndef += '\nreturn null;'
	fndef += '\n}'

	return fndef

def convert_constants(lines):
	constants = {
		'ERROR': [{'name': 'NO_MEMORY', 'value': 12},
			  {'name': 'INVALID', 'value': 22},
			  {'name': 'TIMED_OUT', 'value': 110},
			  {'name': 'ALREADY', 'value': 114}]
	}

	for l in lines:
		m = re.search(r'#define\s+WCALL_(\w+)\s+(.*)', l)
		if m:
			name = m.group(1)
			value = m.group(2).rstrip()
			grp = name.split('_')[0]
			name = name[len(grp)+1:]

			if grp not in constants:
				constants[grp] = []
			constants[grp].append({'name': name, 'value': value})

	del constants['VERSION']

	ctext = ''
	ktext = ''
	for grpname in constants:
		grp = constants[grpname]

		parts = grp[0]['name'].split('_')

		while len(parts) > 1:
			testp = parts[0]
			common = True
			for c in grp:
				if not c['name'].startswith(testp):
					common = False
					break

			if common:
				grpname += '_' + testp
				for c in grp:
					c['name'] = c['name'][len(testp) + 1:]
			else:
				break
			parts = grp[0]['name'].split('_')

		ctext += 'export enum {} {{\n'.format(grpname)
		for c in range(len(grp) - 1):
			ctext += '{} = {},\n'.format(grp[c]['name'], grp[c]['value'])
		ctext += '{} = {},\n'.format(grp[-1]['name'], grp[-1]['value'])
		ctext += '}\n'
		ctext += '\n'

		ktext += '@JsName("{}")\n'.format(grpname)
		ktext += 'external object {} : JsAny {{\n'.format(grpname)
		for c in range(len(grp) - 1):
			ktext += 'val {}: Int\n'.format(grp[c]['name'])
		ktext += 'val {}: Int\n'.format(grp[-1]['name'])
		ktext += '}\n'
		ktext += '\n'	

	return ctext, ktext

def convert_structs(lines):
	state = 0
	stext = ''
	ktext = ''
	ignore_structs = ['wcall_members']
	convert_names = {
		'audio_state': 'aestab',
		'video_recv': 'vrecv'
	}

	for l in lines:
		if state == 0:
			m = re.search(r'struct (\w+) {', l)
			if m:
				if m.group(1) not in ignore_structs:
					sname = convert_to_camel(m.group(1), True)
					state = 1
					stext += 'export interface {}'.format(sname) + ' {\n'
					ktext += 'external interface {} : JsAny'.format(sname) + ' {\n'
		elif state == 1:
			m = re.search('};', l)
			if m:
				state = 0
				stext += '\n};\n'
				ktext += '\n}\n'
			else:
				param = parse_param(l.rstrip().replace('\t', '').replace(';', ''))
				if param:
					if param[0] in convert_names:
						pname = convert_names[param[0]]
					else:
						pname = param[0]
					stext += '\t{}: {};\n'.format(pname, param[2])
					ktext += '\t val {}: {}\n'.format(pname, jsts2kt(param[2]))

	return stext, ktext

def convert_callbacks(lines):
	state = 0
	ctext = ''
	for l in lines:
		if state == 0:
			m = re.search(r'typedef\s+\w+\s+\(\w+\)\(.*', l)
			if m:
				fn = l.strip().rstrip()
				if ';' in l:
					ctext += convert_cb(fn)
					fn = ''
				else:
					state = 1

		elif state == 1:
			fn += ' ' + l.strip().rstrip()
			if ';' in l:
				ctext += convert_cb(fn)
				fn = ''
				state = 0

	return ctext

def convert_functions(lines):
	state = 0
	ftext = ''
	kftext = ''
	for l in lines:
		if state == 0:
			m = re.search(r'\w+\s+\w+\(.*', l)
			if m:
				fn = l.strip().rstrip()
				if ';' in l:
					fx, ktfn = convert_fn(fn)
					ftext += fx
					kftext += ktfn
					fn = ''
				else:
					state = 1

		elif state == 1:
			fn += ' ' + l.strip().rstrip()
			if ';' in l:
				fx, ktfn = convert_fn(fn)
				ftext += fx
				kftext += ktfn
				fn = ''
				state = 0

	return ftext, kftext

def insert_ktheaders(cpnotice):
	kt_write_helper.write_blk(cpnotice)

	kt_modification_warning = """
				/*
			    ============================================================================
				WARNING: THIS FILE IS AUTO-GENERATED. DO NOT EDIT MANUALLY.
				Any manual modifications will be overwritten during the next avs build
				============================================================================
				*/
				"""
	kt_write_helper.write_blk(kt_modification_warning)

	kt_headers = """
				@file:OptIn(kotlin.js.ExperimentalWasmJsInterop::class)
				@file:JsModule("@wireapp/avs")
				
				package com.wire.avskmp

				import kotlin.js.JsAny
				"""
	kt_write_helper.write_blk(kt_headers)

def embed_ktfunctions(lines):
	kt_write_helper.write_blk('external fun getAvsInstance(): JsAny\n')
	kt_write_helper.write_blk('external class Wcall(em_module: JsAny) : JsAny {{\n{}}}'.format(lines))


if __name__ == '__main__':

	if len(sys.argv) != 4:
		print('usage: {} <path to avs_wcall.h> <path to template> <path to dest>'.format(sys.argv[0]))
		exit()

	data = open(sys.argv[1]).read()
	tlines = open(sys.argv[2]).readlines()
	fname = sys.argv[3]
	tsfile = open(fname, 'w')
	ts_write_helper = Write_Helper(tsfile)

	# Get name of output file and form a .kt file
	basename, _ = os.path.splitext(fname)
	ktfname = basename + ".kt.tmp"
	ktfile = open(ktfname, 'w')
	kt_write_helper = Write_Helper(ktfile)

	cpnotice = None
	start = data.find('/*')
	while start > -1:
		extra = None
		end = data.find('*/', start + 1)
		if end < 0:
			break

		if data[start:end+2] == '/*optional*/':
			extra = ' __optional'

		if data[start:end+2] == '/*bool*/':
			extra = ' __bool'

		if data[start:end+2] == '/*any*/':
			extra = ' __any'

		if start > 0:
			newdata = data[:start - 1]
		else:
			newdata = ''

		if not cpnotice:
			cpnotice = data[start:end+2]

		if extra:
			newdata += extra
			extra = None

		newdata += data[end+2:]
		data = newdata
		start = data.find('/*')

	fn = ''
	state = 0

	lines = data.split('\n')

	if cpnotice:
		tsfile.write(cpnotice)

	insert_ktheaders(cpnotice)

	for tl in tlines:
		m = re.match(r'%(\w+)%', tl)
		if m:
			pl = m.group(1)
			if pl == 'CONSTANTS':
				ctext, ktext = convert_constants(lines)
				ts_write_helper.write_blk(ctext)
				kt_write_helper.write_blk(ktext)
			if pl == 'STRUCTS':
				ctext, ktext = convert_structs(lines)
				ts_write_helper.write_blk(ctext)
				kt_write_helper.write_blk(ktext)
			elif pl == 'CALLBACK_TYPES':
				ts_write_helper.write_blk(convert_callbacks(lines))
			elif pl == 'FUNCTIONS':
				ctext, ktext = convert_functions(lines)
				ts_write_helper.write_blk(ctext)
				embed_ktfunctions(ktext)

		else:
			ts_write_helper.write_ln(tl)

