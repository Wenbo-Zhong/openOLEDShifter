# SPDX-License-Identifier: GPL-3.0-only
# Copyright (c) 2026 OpenOLEDShifter contributors

from PIL import Image,ImageDraw
from pathlib import Path
S=1024
im=Image.new('RGBA',(S,S),(0,0,0,0));d=ImageDraw.Draw(im)
d.rounded_rectangle((32,32,992,992),radius=224,fill='#111C2B')
d.rounded_rectangle((164,220,860,744),radius=80,fill='#162D41',outline='#63E6D6',width=44)
d.line((512,744,512,824),fill='#63E6D6',width=40)
d.rounded_rectangle((352,808,672,848),radius=20,fill='#63E6D6')
# A bright pixel moves from the outlined source cell towards the upper right.
d.rounded_rectangle((310,430,470,590),radius=24,outline='#6F93AE',width=26)
d.rounded_rectangle((554,310,714,470),radius=24,fill='#63E6D6')
d.line((462,506,598,506,598,442),fill='#FFFFFF',width=30,joint='curve')
d.polygon(((554,459),(598,415),(642,459)),fill='#FFFFFF')
out=Path('assets');preview=im.resize((512,512),Image.Resampling.LANCZOS);preview.save(out/'app-icon.png')
im.resize((256,256),Image.Resampling.LANCZOS).save(out/'app.ico',sizes=[(16,16),(20,20),(24,24),(32,32),(40,40),(48,48),(64,64),(128,128),(256,256)])
