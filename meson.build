project(
	'mako',
	'c',
	version: '1.1.0',
	license: 'MIT',
	meson_version: '>=0.47.0',
	default_options: [
		'c_std=c11',
		'warning_level=2',
		'werror=true',
	],
)

add_project_arguments('-Wno-unused-parameter', language: 'c')
add_project_arguments('-Wno-missing-braces', language: 'c')

mako_inc = include_directories('include')

cairo = dependency('cairo')
pango = dependency('pango')
pangocairo = dependency('pangocairo')
sdbus = dependency(get_option('sdbus-provider'), required: get_option('sdbus'))
subd = dependency('subd', required: not sdbus.found())
wayland_client = dependency('wayland-client')
wayland_protos = dependency('wayland-protocols', version: '>=1.14')

mako_deps = []

if sdbus.found()
	mako_deps += sdbus
	dbus_dir = 'dbus'
	add_project_arguments('-DHAS_SDBUS')
else
	mako_deps += subd
	dbus_dir = 'dbus.subd'
endif

subdir('protocol')

mako_deps += [
	cairo,
	client_protos,
	sdbus,
	pango,
	pangocairo,
	wayland_client,
]

# epoll is a separate library in FreeBSD
if host_machine.system() == 'freebsd'
	mako_deps += dependency('epoll-shim')
endif

executable(
	'mako',
	files([
		'config.c',
		'event-loop.c',
		dbus_dir + '/dbus.c',
		dbus_dir + '/mako.c',
		dbus_dir + '/xdg.c',
		'main.c',
		'notification.c',
		'pool-buffer.c',
		'render.c',
		'wayland.c',
		'criteria.c',
		'types.c',
	]),
	dependencies: mako_deps,
	include_directories: [mako_inc],
	install: true,
)

makoctl = sdbus.found() ? 'makoctl' : 'makoctl.libdbus'

install_data(
	makoctl,
	rename: 'makoctl',
	install_dir: get_option('bindir'),
	install_mode: 'rwxr-xr-x',
)

scdoc = find_program('scdoc', required: false)

if scdoc.found()
	sh = find_program('sh')

	man_pages = ['mako.1.scd', 'makoctl.1.scd']

	mandir = get_option('mandir')

	foreach src : man_pages
		topic = src.split('.')[0]
		section = src.split('.')[1]
		output = '@0@.@1@'.format(topic, section)

		custom_target(
			output,
			input: src,
			output: output,
			command: [
				sh, '-c', '@0@ < @INPUT@ > @1@'.format(scdoc.path(), output)
			],
			install: true,
			install_dir: '@0@/man@1@'.format(mandir, section)
		)
	endforeach
endif
