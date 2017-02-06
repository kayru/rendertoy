import sublime, sublime_plugin
import functools
import html
import os.path
import re

pluginInstances = []

def plugin_unloaded():
    global pluginInstances
    for i in range(len(pluginInstances)):
        pluginInstances[i].shouldUnload = True
  
class RenderToy(sublime_plugin.ViewEventListener):  
    show_errors_inline = True

    def update_phantoms(self):
        stylesheet = '''
            <style>
                div.error {
                    padding: 0.4rem 0 0.4rem 0.7rem;
                    margin: 0.2rem 0;
                    border-radius: 2px;
                }

                div.error span.message {
                    padding-right: 0.7rem;
                }

                div.error a {
                    text-decoration: inherit;
                    padding: 0.35rem 0.7rem 0.45rem 0.8rem;
                    position: relative;
                    bottom: 0.05rem;
                    border-radius: 0 2px 2px 0;
                    font-weight: bold;
                }
                html.dark div.error a {
                    background-color: #00000018;
                }
                html.light div.error a {
                    background-color: #ffffff18;
                }
            </style>
        '''

        phantoms = []
        self.phantom_set = sublime.PhantomSet(self.view, "RenderToy")

        for line, error in self.errors.items():
            lineTempl = '<span class="message">%s</span>'
            text = '<br />'.join([(lineTempl % html.escape(e[1], quote=False)) for e in error])
            column = error[0][0]

            pt = self.view.text_point(line - 1, column - 1)
            phantoms.append(sublime.Phantom(
                sublime.Region(pt, self.view.line(pt).b),
                ('<body id=inline-error>' + stylesheet +
                    '<div class="error">' +
                    text +
                    #'<a href=hide>' + chr(0x00D7) + '</a></div>' +
                    '</body>'),
                sublime.LAYOUT_BELOW))
            self.phantom_set.update(phantoms)

    def hide_phantoms(self):
        self.phantom_set = None
        self.show_errors_inline = False

    def on_phantom_navigate(self, url):
        self.hide_phantoms()

    def onUpdate(self):
        errorsFilePath = self.view.file_name() + '.errors'

        if not os.path.isfile(errorsFilePath):
            self.errors = {}
            self.update_phantoms()
            return

        try:
            mtime = os.path.getmtime(errorsFilePath)
            if self.prevMtime == mtime:
                return
            with open(errorsFilePath) as f:
                errors = f.readlines()
                errors = [x.strip() for x in errors if len(x.strip()) > 0]

                self.prevMtime = mtime
                self.errors = {}

                for e in errors:
                    m = self.errorRe.match(e)
                    if m:
                        line = int(m.group(2))
                        column = int(m.group(1))
                        text = m.group(3)
                        if line in self.errors:
                            self.errors[line].append((column, text))
                        else:
                            self.errors[line] = [(column, text)]

                self.update_phantoms()
        except BaseException as e:
            print('Could not parse "%s": %s' % (errorsFilePath, e))

    def handleTimeout(self, view):
        if self.shouldUnload:
            self.hide_phantoms()
        else:
            self.onUpdate()
            sublime.set_timeout(functools.partial(self.handleTimeout, self.view), 250)

    def __init__(self, view):
        fname = view.file_name()
        if fname and fname[-5:] == '.glsl':
            global pluginInstances
            pluginInstances.append(self)
            self.shouldUnload = False
            self.view = view
            self.prevMtime = 0
            self.errorRe = re.compile(r'ERROR: (.+):(\d+): (.*)')
            self.handleTimeout(self.view)
