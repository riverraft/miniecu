#!/usr/bin/env python

import sys
from os import path

# add path to tools dir
MODULE_PATH = path.abspath(path.dirname(__file__))
TOOLS_PATH = path.dirname(MODULE_PATH)
if TOOLS_PATH not in sys.path:
    sys.path.append(TOOLS_PATH)


import logging
from comm import CommThread
from gi.repository import Gtk
from ui.ccgui import CCGuiApplication


def main():
    logging.basicConfig(level=logging.DEBUG)

    app = CCGuiApplication()
    Gtk.main()
    logging.shutdown()


if __name__ == '__main__':
    main()
