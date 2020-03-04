
import re
import sys

callbacks = {}

ind = 2
cind = 0

def indent_diff(l):
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
	
def write_ln(f, l):
	global ind
	global cind

	blnk = '                                               '
	l = l.strip().rstrip()
	d = indent_diff(l)
	cind += d[0]
	f.write('{}{}\n'.format(blnk[:cind * ind], l))
	cind += d[1]

def write_blk(f, blk):
	if blk:
		lines = blk.split('\n')
		for l in lines:
			write_ln(f, l)

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

def parse_param(param):
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
		tsType += ' | undefined'

	return (pname, c2jstype(ptype), tsType)

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

	m = re.search('(\w+)\s+(\w+)\((.*)\);', fn)
	if m:
		ret = m.group(1)
		fname = m.group(2)
		args = m.group(3)
		args = re.sub('\s+\/\*.*?\*\/', '', args)
		args = args.split(', ')

		if fname in ignored:
			return ''

		argtypes = []
		argnames = []
		for a in args:
			ainfo = parse_param(a)
			if ainfo:
				argnames.append(ainfo[0])
				argtypes.append(ainfo[1])

		fndef = '{}('.format(convert_to_camel(fname.replace('wcall_', '')))
		i = 0
		for a in argnames:
			if i > 0:
				fndef += ','
			fndef += '\n{}: {}'.format(a, c2tstype(argtypes[i]))
			if a == 'arg':
				fndef += ' = 0'
			i += 1
		if i > 0:
			fndef += '\n'
		fndef += '): {} {{\n'.format(c2tstype(ret))

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
				fndef += '\'{}\');\n'.format(cb['proto'])
				argnames[i] = 'fn_{}'.format(argnames[i])
				argtypes[i] = 'number'
                                
		if ret == 'void':
			fndef += ''
		else:
			fndef += 'return '

                if fname == 'wcall_set_log_handler':
                        fndef += 'logFn = logh;'
                        
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

		return fndef
	return ''

def convert_cb(fn):

	ignore = ['wcall_render_frame_h']
	m = re.search('typedef\s+(\w+)\s+\((\w+)\)\((.*)\);', fn)
	if m and m.group(2) not in ignore:
		ret = m.group(1)
		fname = m.group(2)
		args = m.group(3)
		args = re.sub('\s+\/\*.*?\*\/', '', args)
		args = args.split(', ')
		argnames = []

		tsname = convert_to_camel(fname, True) + 'andler'
		fndef =  'export type {} = (\n'.format(tsname)

		if ret == 'void':
			fproto = 'v'
		else:
			fproto = c2jstype(ret)[0]
		first = True
		for a in args:
			ainfo = parse_param(a)
			if ainfo:
				argnames.append(ainfo[0])
				fproto += ainfo[1][0]
				if first:
					first = False
				else:
					fndef += ',\n'
				fndef += '{}: {}'.format(ainfo[0], ainfo[2])

		cb = {
			'name': fname,
			'tsname': tsname,
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
		m = re.search('#define\s+WCALL_(\w+)\s+(.*)', l)
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

	return ctext

def convert_callbacks(lines):
	state = 0
	ctext = ''
	for l in lines:
		if state == 0:
			m = re.search('typedef\s+\w+\s+\(\w+\)\(.*', l)
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
	for l in lines:
		if state == 0:
			m = re.search('\w+\s+\w+\(.*', l)
			if m:
				fn = l.strip().rstrip()
				if ';' in l:
					ftext += convert_fn(fn)
					fn = ''
				else:
					state = 1

		elif state == 1:
			fn += ' ' + l.strip().rstrip()
			if ';' in l:
				ftext += convert_fn(fn)
				fn = ''
				state = 0
	return ftext

if __name__ == '__main__':

	if len(sys.argv) != 4:
		print('usage: {} <path to avs_wcall.h> <path to template> <path to dest>'.format(sys.argv[0]))
		exit()

	data = open(sys.argv[1]).read()
	tlines = open(sys.argv[2]).readlines()
	outfile = open(sys.argv[3], 'w')

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
		outfile.write(cpnotice)

	for tl in tlines:
		m = re.match('%(\w+)%', tl)
		if m:
			pl = m.group(1)
			if pl == 'CONSTANTS':
				write_blk(outfile, convert_constants(lines))
			elif pl == 'CALLBACK_TYPES':
				write_blk(outfile, convert_callbacks(lines))
			elif pl == 'FUNCTIONS':
				write_blk(outfile, convert_functions(lines))
		else:
			write_ln(outfile, tl)

