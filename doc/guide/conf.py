import sphinx_rtd_theme

project = 'Basic MAC'
copyright = '2020, Michael Kuyper'
author = 'Michael Kuyper'

extensions = [ 'sphinx_rtd_theme', ]

templates_path = [ '_templates' ]

exclude_patterns = [ '_build' ]

html_theme = 'sphinx_rtd_theme'
html_static_path = [ '_static' ]
