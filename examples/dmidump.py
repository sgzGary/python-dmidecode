#!/usr/bin/env python2.4
import dmidecode
import sys
from pprint import pprint

#. Test reading the dump...
print "*** bios ***\n";      pprint(dmidecode.bios())
print "*** system ***\n";    pprint(dmidecode.system())
print "*** system ***\n";    pprint(dmidecode.system())
print "*** baseboard ***\n"; pprint(dmidecode.baseboard())
print "*** chassis ***\n";   pprint(dmidecode.chassis())
print "*** processor ***\n"; pprint(dmidecode.processor())
print "*** memory ***\n";    pprint(dmidecode.memory())
print "*** cache ***\n";     pprint(dmidecode.cache())
print "*** connector ***\n"; pprint(dmidecode.connector())
print "*** slot ***\n";      pprint(dmidecode.slot())

for v in dmidecode.memory().values():
  if type(v) == dict and v['dmi_type'] == 17:
    pprint(v['data']['Size']),

pprint(dmidecode.type(3))
