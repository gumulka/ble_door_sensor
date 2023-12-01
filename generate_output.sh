#!/bin/bash

set -eux

KICAD=kicad-cli
PROJECT=ble_door_sensor


## cleanup
rm -rf Gerbers/ ${PROJECT}.zip Fabrication/ Output/

## Gerber files
mkdir Gerbers
cd Gerbers
${KICAD} pcb export gerbers ../${PROJECT}.kicad_pcb
${KICAD} pcb export drill ../${PROJECT}.kicad_pcb
cd -
zip -qrD ${PROJECT} Gerbers

## BOM
mkdir Fabrication
${KICAD} sch export netlist ${PROJECT}.kicad_sch --format kicadxml -o Fabrication/${PROJECT}.xml
/usr/bin/python3 "bom_csv_grouped_extra.py" "Fabrication/${PROJECT}.xml" "Fabrication/${PROJECT}_bom.csv" "LCSC#"
if command -v kicost &> /dev/null
then
    kicost -i Fabrication/${PROJECT}.xml --eda kicad -o Fabrication/${PROJECT}.ods -n 1000 --currency EUR --overwrite --translate_fields LCSC# lcsc_num
fi

## Position file
${KICAD} pcb export pos ${PROJECT}.kicad_pcb --units mm --format csv -o ${PROJECT}.pos
sed -i "s/Ref/\"Designator\"/g" ${PROJECT}.pos
sed -i "s/PosX/\"Mid X\"/g" ${PROJECT}.pos
sed -i "s/PosY/\"Mid Y\"/g" ${PROJECT}.pos
sed -i "s/Rot/\"Rotation\"/g" ${PROJECT}.pos
sed -i "s/Side/\"Layer\"/g" ${PROJECT}.pos
sed -i "s/Val/\"Value\"/g" ${PROJECT}.pos
./fix_rotation -i ${PROJECT}.pos -o Fabrication/${PROJECT}_pos.csv
rm ${PROJECT}.pos



## Schematics
mkdir Output
${KICAD} sch export pdf ${PROJECT}.kicad_sch -o Output/${PROJECT}.pdf

## STEP file
${KICAD} pcb export step ${PROJECT}.kicad_pcb -o Output/${PROJECT}.step
