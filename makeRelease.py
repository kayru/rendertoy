import shutil
import os

if os.path.exists('release'):
	shutil.rmtree('release')
os.mkdir('release')

os.system('tools\\tundra2.exe release')
shutil.copy('t2-output/win64-msvc-release-default/rendertoy.exe', 'release')
shutil.copytree('data', 'release/data')
shutil.copytree('tools/sublimePlugin', 'release/sublimePlugin')

releaseVersion = '0.1'

shutil.make_archive('rendertoy_' + releaseVersion + '_64', 'zip', 'release')
