project('kusanagi', 'cpp', 'c',
  version: '5.0.0-dev',
  license: 'MIT',
  meson_version: '>= 1.3',
  default_options: [
    'cpp_std=c++23',
    'warning_level=2',
    'buildtype=debugoptimized',
    'b_ndebug=if-release',
  ])

add_project_arguments('-Wno-unused-parameter', '-Wno-missing-field-initializers', '-DKUSANAGI_VERSION="' + meson.project_version() + '"', language: 'cpp')
add_project_arguments('-Wno-unused-parameter', language: 'c')

wayland_client = dependency('wayland-client')
wayland_egl = dependency('wayland-egl')
wayland_protocols = dependency('wayland-protocols')
egl = dependency('egl')
glesv2 = dependency('glesv2')
freetype = dependency('freetype2')
harfbuzz = dependency('harfbuzz')
fontconfig = dependency('fontconfig')
tomlpp = dependency('tomlplusplus')

# ---------------------------------------------------------------- wayland protocols
scanner = find_program(dependency('wayland-scanner', native: true).get_variable('wayland_scanner'))
wp = wayland_protocols.get_variable('pkgdatadir')
protocol_xml = [
  wp / 'stable/xdg-shell/xdg-shell.xml',
  wp / 'stable/viewporter/viewporter.xml',
  wp / 'stable/tablet/tablet-v2.xml',                 # cursor-shape refers to tablet tools
  wp / 'staging/fractional-scale/fractional-scale-v1.xml',
  wp / 'staging/cursor-shape/cursor-shape-v1.xml',
  wp / 'staging/ext-workspace/ext-workspace-v1.xml',
  wp / 'unstable/xdg-output/xdg-output-unstable-v1.xml',
  'protocols/wlr-layer-shell-unstable-v1.xml',
  'protocols/wlr-foreign-toplevel-management-unstable-v1.xml',
]
protocol_src = []
foreach xml : protocol_xml
  name = xml.split('/')[-1].replace('.xml', '')
  protocol_src += custom_target(name + '-client-header',
    input: xml, output: name + '-client.h',
    command: [scanner, 'client-header', '@INPUT@', '@OUTPUT@'])
  protocol_src += custom_target(name + '-code',
    input: xml, output: name + '-protocol.c',
    command: [scanner, 'private-code', '@INPUT@', '@OUTPUT@'])
endforeach

# ---------------------------------------------------------------- the shell
src = files(
  'src/main.cpp',
  'src/app/EventLoop.cpp',
  'src/app/Log.cpp',
  'src/app/Process.cpp',
  'src/util/Json.cpp',
  'src/util/Paths.cpp',
  'src/util/Time.cpp',
  'src/system/SysInfo.cpp',
  'src/wayland/Display.cpp',
  'src/wayland/Output.cpp',
  'src/wayland/Seat.cpp',
  'src/wayland/LayerSurface.cpp',
  'src/renderer/Gl.cpp',
  'src/renderer/Renderer.cpp',
  'src/renderer/Text.cpp',
  'src/anim/Animation.cpp',
  'src/ui/Widget.cpp',
  'src/ui/Widgets.cpp',
  'src/config/Config.cpp',
  'src/config/Theme.cpp',
  'src/ipc/Ipc.cpp',
  'src/system/Wm.cpp',
  'src/shell/Shell.cpp',
  'src/shell/Bar.cpp',
  'src/shell/WsIndicator.cpp',
)

executable('kusanagi', src, protocol_src,
  include_directories: include_directories('src'),
  dependencies: [wayland_client, wayland_egl, egl, glesv2, freetype, harfbuzz, fontconfig, tomlpp],
  install: true)

install_subdir('../assets', install_dir: get_option('datadir') / 'kusanagi')
