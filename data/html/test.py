#!/usr/bin/python

print("Content-Type: text/html\r\n\r\n", end = '')

import os
print("REQUEST_METHOD: " + os.environ['REQUEST_METHOD'] + "")
