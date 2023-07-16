; @file data.asm
; @author Daniel Starke
; @copyright Copyright 2023 Daniel Starke
; @date 2023-07-03
; @version 2023-07-13

.include "hdr.asm"


.section ".rodata1" superfree

bg1Tiles:
.incbin "bg1.chr"
bg1TilesEnd:

fg1Tiles:
.incbin "fg1.chr"
fg1TilesEnd:

fg2Tiles:
.incbin "fg2.chr"
fg2TilesEnd

p12Tiles:
.incbin "p12.chr"
p12TilesEnd

.ends


.section ".rodata2" superfree

bg1Map:
.incbin "bg1.map"
bg1MapEnd:

bg2Map:
.incbin "bg2.map"
bg2MapEnd:

fg1Map:
.incbin "fg1.map"
fg1MapEnd:

optionsMap:
.incbin "options.map"
optionsMapEnd:

fieldMap:
.incbin "field.map"
fieldMapEnd:

bg1Pal:
.incbin "bg1.pal"
bg1PalEnd:

fg1Pal:
.incbin "fg1.pal"
fg1PalEnd:

fg2Pal:
.incbin "fg2.pal"
fg2PalEnd:

p12Pal:
.incbin "p12.pal"
p12PalEnd:

.ends


.ifdef HAS_SFX
.section ".rodata3" superfree

sfx1:
.incbin "sfx1.brr"
sfx1End:

.ends
.endif