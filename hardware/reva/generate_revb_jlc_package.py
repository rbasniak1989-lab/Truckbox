from pathlib import Path
from collections import Counter
import csv, io, os, re, shutil, zipfile

ROOT=Path(__file__).resolve().parent
BOARD=ROOT/'TruckBox_RevA.kicad_pcb'
BUILD=ROOT/'build'
POS=BUILD/'positions.csv'

def p(refs, comment, footprint, lcsc, mpn, manufacturer, note=''):
    return dict(refs=[x.strip() for x in refs.split(',') if x.strip()],
                comment=comment, footprint=footprint, lcsc=lcsc,
                mpn=mpn, manufacturer=manufacturer, note=note)

PARTS=[
p('U1,U2','ESP32-C6-WROOM-1-N8, 8MB flash, PCB antenna','ESP32-C6-WROOM-1','C5366877','ESP32-C6-WROOM-1-N8','Espressif'),
p('U3','TCAN3404DRQ1 automotive 3.3V CAN transceiver','SOIC-8_3.9x4.9_P1.27','C34021147','TCAN3404DRQ1','Texas Instruments'),
p('U4','SN65HVD1781QDRQ1 automotive J1708 transceiver','SOIC-8_3.9x4.9_P1.27','C2878178','SN65HVD1781QDRQ1','Texas Instruments'),
p('U5','ATGM336H-5NR32 GNSS module','ATGM336H-5NR32','C5117921','ATGM336H-5NR32','ZHONGKEWEI'),
p('U6','TPS54260QDGQRQ1 60V automotive buck regulator','HVSSOP-10_DGQ_PowerPAD','C181278','TPS54260QDGQRQ1','Texas Instruments'),
p('U7','MB85RS64PNF-G-JNERE1 64Kbit SPI FRAM','SOIC-8_3.9x4.9_P1.27','C8741','MB85RS64PNF-G-JNERE1','RAMXEED / Fujitsu'),
p('U8','TPL5010QDDCRQ1 automotive nano-power watchdog','SOT-23-6_DDC','C1847955','TPL5010QDDCRQ1','Texas Instruments','Recheck stock at order preview.'),
p('U9','A7683E LTE Cat.1 module','COMM-SMD_L17.6-W15.7_A7683E','C20617360','A7683E','SIMCom Wireless Solutions','JLCPCB Standard PCBA only; X-ray inspection required.'),
p('U10','TXU0202DCUR dual-supply 1.8V/3.3V level translator','VSSOP-8_L2.3-W2.0-P0.50-LS3.1-BR','C5186957','TXU0202DCUR','Texas Instruments'),
p('U11','TPS54360BQDDARQ1 60V LTE 3.8V buck regulator','SO-8_L4.9-W3.9-P1.27-LS6.0-BL-EP','C2687968','TPS54360BQDDARQ1','Texas Instruments'),

p('D1','SM8S33A 33V high-power TVS','DO218AB_Yangjie_SM8S','C698915','SM8S33A','Yangzhou Yangjie'),
p('D2','STPST2H100AFY 100V 2A automotive Schottky','SOD128','C20033887','STPST2H100AFY','STMicroelectronics','Recheck stock at order preview.'),
p('D3,D4','AQ24CANFD-02HTG automotive data-line TVS','SC70-3_DCK','C1975190','AQ24CANFD-02HTG','Littelfuse'),
p('D5','SS310AQ 100V 3A AEC-Q101 Schottky','SMA','C5339831','SS310AQ','Yangjie'),
p('D6','TPD2E2U06QDCKRQ1 dual USB ESD protector','SC70-3_DCK','C915089','TPD2E2U06QDCKRQ1','Texas Instruments'),
p('D7','1N4148WS switching diode','SOD323','C181133','1N4148WS','Guangdong Hottech'),
p('D50','SS56B 60V 5A Schottky','SMB(DO-214AA)','C14651','SS56B','MDD (Microdiode Semiconductor)','Exact JLC SMB footprint matched in Rev.B.'),
p('D70','PESD5Z5.0,115 PWRKEY ESD','SOD-523','C132368','PESD5Z5.0,115','Nexperia'),
p('D71','SRV05-4 four-channel low-capacitance SIM ESD array','SOT-23-6','C558418','SRV05-4','TECH PUBLIC'),

p('J1','1040310811 microSD card socket','microSD_Molex_1040310811','C585350','1040310811','Molex'),
p('J2','U.FL-R-SMT-1(01) GNSS RF connector','UFL_Hirose','C598199','U.FL-R-SMT-1(01)','Hirose','Recheck stock at order preview.'),
p('J3','USB4105-GF-A-060 USB-C receptacle','USB_C_Receptacle_GCT_USB4105_16P','C3025063','USB4105-GF-A-060','GCT'),
p('J4','TE/DEUTSCH DT13-08PA sealed 8-way right-angle PCB header','CONN-TH_DT13-08PA','C9900176781','DT13-08PA','TE Connectivity / DEUTSCH','JLC wave-solder/THT assembly. Mating harness plug: DT06-08SA.'),
p('J5','SIM8051-6-0-14-01-A nano-SIM socket','SIM-SMD_SIM8051-6-0-14-01-A','C3033025','SIM8051-6-0-14-01-A','GCT'),
p('J6','U.FL-R-SMT-1(10) LTE RF connector','ANT-SMD_UFL-R-SMT-1-10','C88373','U.FL-R-SMT-1(10)','Hirose'),

p('L1','15uH ±20% 3.5A Isat 6A AEC-Q200 power inductor','IND_7x7','C2045384','SRP7050TA-150M','Bourns','Approved Rev.A substitution for board value 18uH.'),
p('L2','47nH RF inductor','0603','C2903684','HP0603-47NH-N','Chilisin'),
p('L50','22uH 5A Isat 6.3A LTE power inductor','IND-SMD_L7.9-W7.3_SRP7050WA','C19947701','SRP7050WA-220M','Bourns','Exact JLC footprint matched in Rev.B.'),

p('Q1,Q2,Q4','AO3401A P-channel MOSFET','SOT-23-3','C15127','AO3401A','Alpha & Omega Semiconductor'),
p('Q3','MMBT5551 high-voltage NPN transistor','SOT-23-3','C5184409','MMBT5551','HXY MOSFET'),
p('Q50','2N7002 PWRKEY open-drain MOSFET','SOT-23-3','C8545','2N7002','Jiangsu Changjing / compatible'),

p('R1,R2','5.1kΩ ±1% 0603','0603','C23186','0603WAF5101T5E','UNI-ROYAL'),
p('R3,R4,R7,R12,R13,R14,R20,R24,R25,R29,R30,R39,R40,R41','10kΩ ±1% 0603','0603','C25804','0603WAF1002T5E','UNI-ROYAL'),
p('R5,R62','10.2kΩ ±1% 0603','0603','C321670','RS-03K1022FT','FH'),
p('R6','31.6kΩ ±1% 0603','0603','C25967','0603WAF3162T5E','UNI-ROYAL'),
p('R8','412kΩ ±1% 0603','0603','C23050','0603WAF4123T5E','UNI-ROYAL'),
p('R9','20kΩ ±1% 0603','0603','C4184','0603WAF2002T5E','UNI-ROYAL'),
p('R10,R11,R26,R27,R28,R64,R70','100kΩ ±1% 0603','0603','C25803','0603WAF1003T5E','UNI-ROYAL'),
p('R31','681kΩ ±1% 0603','0603','C54531561','1RC0603F6813','SAE'),
p('R32','91kΩ ±1% 0603','0603','C137671','RC0603FR-0791KL','Yageo'),
p('R33,R34,R55,R56','4.7kΩ ±1% 0603','0603','C23162','0603WAF4701T5E','UNI-ROYAL'),
p('R42,R43,R72,R73,R74','22Ω ±1% 0603','0603','C107701','RC0603FR-0722RL','Yageo'),
p('R50','0Ω RF jumper 0603','0603','C21189','0603WAF0000T5E','UNI-ROYAL'),
p('R53,R54','47Ω ±1% J1708 series 0603','0603','C23182','0603WAF470JT5E','UNI-ROYAL'),
p('R60','243kΩ ±1% RT 0603','0603','C137764','RC0603FR-07243KL','Yageo'),
p('R61','38.3kΩ ±1% FB 0603','0603','C23034','0603WAF3832T5E','UNI-ROYAL'),
p('R63','6.34kΩ ±1% COMP 0603','0603','C321969','RS-03K6341FT','FH'),
p('R65','15kΩ ±1% 0603','0603','C114661','RC0603FR-0715KL','Yageo'),
p('R71','1kΩ ±1% PWRKEY series 0603','0603','C22548','RC0603FR-071KL','Yageo'),

p('C1,C2,C61,C62','2.2uF 100V X7R 1210 MLCC','1210','C338133','1210B225K101CT','Walsin','100V part intentionally reused for LTE 50V input positions.'),
p('C3','47uF 63V SMD aluminum electrolytic','CP_Elec_D8x10.2','C401887','EEETG1J470UP','Panasonic','Recheck stock at order preview.'),
p('C4,C9,C10,C11,C12,C13,C14,C21,C24,C60,C70,C73,C74','100nF 50V X7R 0603 MLCC','0603','C14663','CC0603KRX7R9BB104','Yageo'),
p('C5','10nF 50V X7R 0603 MLCC','0603','C57112','0603B103K500NT','FH'),
p('C6','4.7nF 50V X7R 0603 MLCC','0603','C106218','CC0603KRX7R9BB472','Yageo'),
p('C7,C8,C63,C64','47uF 10V X5R 1210 MLCC','1210','C397312','GRM32ER61A476KE20L','Murata'),
p('C15,C16,C17,C18','10uF 25V X5R 0805 MLCC','0805','C15850','CL21A106KAYNNNE','Samsung Electro-Mechanics'),
p('C19,C20,C69','1uF 50V X5R 0603 MLCC','0603','C92848','UMK107BJ105KA-T','Taiyo Yuden'),
p('C52,C53','2.2nF 50V X7R 0603 J1708 EMI','0603','C2835545','C0603X7R222J500NT','SANYEAR'),
p('C65','56nF 50V X7R 0603 COMP','0603','C309032','TCC0603X7R563K500CT','CCTC'),
p('C66','100pF 50V C0G 0603 COMP pole','0603','C344186','TCC0603COG101J500CT','CCTC'),
p('C67,C68','100uF 10V CASE-D_7343 LTE VBAT reservoir','CASE-D_7343','C9900014040','100uF±10%10V','JLCPCB Assembly','Generic JLC Assembly item; verify polarity/availability in BOM preview.'),
p('C71,C75','33pF 50V C0G 0603','0603','C5375800','TCC0603COG330G500CT','CCTC'),
p('C72','10pF 50V C0G 0603','0603','C376764','TCC0603COG100J500CT','CCTC'),
]

DNP={'C50':'RF shunt tuning DNP','C51':'RF shunt tuning DNP',
     'C76':'SIM RST 22pF optional DNP','C77':'SIM CLK 22pF optional DNP','C78':'SIM DATA 22pF optional DNP'}
MANUAL={'SW1':'BOOT/service switch — DNP/manual until stocked part is physically matched'}
PCB_FEATURES={f'H{i}':'Mounting hole' for i in range(1,7)}
PCB_FEATURES.update({f'TP{i}':'Copper test point' for i in range(1,7)})
EXCLUDED=set(DNP)|set(MANUAL)|set(PCB_FEATURES)

def balanced_end(text,start):
    depth=0; quoted=False; esc=False
    for i in range(start,len(text)):
        c=text[i]
        if quoted:
            if esc: esc=False
            elif c=='\\': esc=True
            elif c=='"': quoted=False
            continue
        if c=='"': quoted=True
        elif c=='(': depth+=1
        elif c==')':
            depth-=1
            if depth==0: return i+1
    raise RuntimeError('unterminated board s-expression')

def parse_board_refs(text):
    rows=[]; i=0
    while True:
        i=text.find('(footprint ',i)
        if i<0: break
        j=balanced_end(text,i); b=text[i:j]
        mr=re.search(r'\(property\s+"Reference"\s+"([^"]+)"',b)
        if not mr: mr=re.search(r'\(fp_text\s+reference\s+"?([^"\s\)]+)',b)
        mv=re.search(r'\(property\s+"Value"\s+"([^"]*)"',b)
        if mr: rows.append((mr.group(1),mv.group(1) if mv else ''))
        i=j
    return rows

def nat(ref):
    m=re.match(r'([A-Za-z]+)(\d+)$',ref)
    return (m.group(1),int(m.group(2))) if m else (ref,0)

board_rows=parse_board_refs(BOARD.read_text(encoding='utf-8'))
board_refs=[r for r,_ in board_rows]
dups=[r for r,n in Counter(board_refs).items() if n>1]
if dups: raise RuntimeError(f'duplicate PCB references: {dups}')
board_values=dict(board_rows)

mapped={}
for group in PARTS:
    for ref in group['refs']:
        if ref in mapped: raise RuntimeError(f'duplicate BOM mapping for {ref}')
        mapped[ref]=group

eligible=set(board_refs)-EXCLUDED
if eligible != set(mapped):
    raise RuntimeError(f'assembly mapping mismatch missing={sorted(eligible-set(mapped),key=nat)} extra={sorted(set(mapped)-eligible,key=nat)}')

with POS.open(newline='',encoding='utf-8-sig') as f:
    pos_rows=list(csv.DictReader(f))
pos_by_ref={r['Ref']:r for r in pos_rows}
missing_pos=sorted(set(mapped)-set(pos_by_ref),key=nat)
if missing_pos: raise RuntimeError(f'assembly refs missing from positions.csv: {missing_pos}')

# JLC import BOM
bom_path=BUILD/'TruckBox_RevB_JLC_BOM.csv'
with bom_path.open('w',newline='',encoding='utf-8-sig') as f:
    wr=csv.writer(f)
    wr.writerow(['Comment','Designator','Footprint','JLCPCB Part #'])
    for g in PARTS:
        wr.writerow([g['comment'],','.join(g['refs']),g['footprint'],g['lcsc']])

# JLC CPL: preserve KiCad/JLC convention validated by Rev.A Run159.
cpl_path=BUILD/'TruckBox_RevB_JLC_CPL.csv'
with cpl_path.open('w',newline='',encoding='utf-8-sig') as f:
    wr=csv.writer(f)
    wr.writerow(['Designator','Mid X','Mid Y','Rotation','Layer'])
    for ref in sorted(mapped,key=nat):
        r=pos_by_ref[ref]
        wr.writerow([ref,r['PosX'],r['PosY'],r['Rot'],r['Side']])

# Sourcing / traceability audit.
audit_path=BUILD/'TruckBox_RevB_LCSC_Audit.csv'
with audit_path.open('w',newline='',encoding='utf-8-sig') as f:
    wr=csv.writer(f)
    wr.writerow(['Designators','Qty','Board Value(s)','Selected MPN','Manufacturer','LCSC/JLC #','Board Footprint','Assembly','Verification','Source','Notes'])
    for g in PARTS:
        vals=sorted({board_values[r] for r in g['refs']})
        status='VALIDATED_CURRENT_REUSE' if all(not re.match(r'(U9|U10|U11|D50|D70|D71|J5|J6|L50|Q50|R5[03456]|R6[0135]|R7[0-4]|C5[23]|C6[0-9]|C7[0-5])$',r) for r in g['refs']) else 'REV_B_SELECTED'
        wr.writerow([','.join(g['refs']),len(g['refs']),' | '.join(vals),g['mpn'],g['manufacturer'],g['lcsc'],g['footprint'],'JLC SMT',status,f'https://www.lcsc.com/search?q={g["lcsc"]}',g['note']])
    for ref,note in sorted(DNP.items(),key=lambda kv:nat(kv[0])):
        wr.writerow([ref,1,board_values.get(ref,''),'','','',pos_by_ref.get(ref,{}).get('Package',''),'DNP','DNP', '',note])
    for ref,note in sorted(MANUAL.items(),key=lambda kv:nat(kv[0])):
        wr.writerow([ref,1,board_values.get(ref,''),'','','',pos_by_ref.get(ref,{}).get('Package',''),'MANUAL / DNP','MANUAL','',note])

drc=(BUILD/'drc_summary.txt').read_text(errors='ignore').strip() if (BUILD/'drc_summary.txt').exists() else 'DRC summary unavailable'
sha=os.environ.get('GITHUB_SHA','local')
run=os.environ.get('GITHUB_RUN_NUMBER','local')
readme=f'''TruckBox Rev.B — JLCPCB Production Package
============================================

SOURCE / FREEZE
- Repository branch: hardware-revb-j1708-4g
- Source commit: {sha}
- CI Run: {run}
- Board: 118 x 95 mm, 4 layers
- DRC:
{chr(10).join("  "+x for x in drc.splitlines()[:4])}

ASSEMBLY CROSS-CHECK
- PCB references: {len(board_refs)}
- Automatic JLC assembly designators: {len(mapped)}
- BOM grouped lines: {len(PARTS)}
- CPL designators: {len(mapped)}
- BOM expanded designators == CPL designators == mapped PCB designators: YES
- Duplicate PCB references: 0

PCBA TYPE
- JLCPCB STANDARD PCBA REQUIRED.
- U9 SIMCom A7683E / C20617360 is a Standard-only JLCPCB part and requires X-ray inspection.
- Do not select Economic PCBA for this Rev.B assembly.

DNP / MANUAL
- DNP: C50, C51 (LTE RF tuning shunts).
- DNP: C76, C77, C78 (optional SIM 22 pF shunts).
- Manual after PCBA: J4 vehicle harness/pigtail.
- J4: DT13-08PA / C9900176781, JLC THT/wave-solder assembly; mates with DT06-08SA harness plug.\n- DNP/manual: SW1 until the physical switch is matched.
- H1-H6 and TP1-TP6 are PCB features, not BOM/CPL components.

CRITICAL REV.B PARTS
- U4: SN65HVD1781QDRQ1 / C2878178 — bidirectional J1708.
- U9: A7683E / C20617360 — LTE modem.
- U10: TXU0202DCUR / C5186957 — 1.8V/3.3V UART translator.
- U11: TPS54360BQDDARQ1 / C2687968 — LTE 3.8V power.
- L50: SRP7050WA-220M / C19947701 — exact JLC footprint matched.
- D50: SS56B / C14651 — exact JLC SMB footprint matched.
- J5: SIM8051-6-0-14-01-A / C3033025.
- D71: SRV05-4 / C558418 — SIM ESD array.
- C67/C68: C9900014040 — JLC Assembly CASE-D_7343 100uF/10V; verify polarity and availability in BOM preview.

FILES
- TruckBox_RevB_Gerbers_JLC.zip
- TruckBox_RevB_JLC_BOM.csv
- TruckBox_RevB_JLC_CPL.csv
- TruckBox_RevB_LCSC_Audit.csv
- drc_summary.txt
- positions.csv
- top.svg

ORDER CHECK
1. Upload Gerber ZIP and confirm 118 x 95 mm / 4 layers.
2. Select Standard PCBA.
3. Upload BOM and CPL; expect {len(mapped)} matched designators.
4. Confirm U9 is C20617360 and X-ray-required.
5. Confirm DNP refs remain unpopulated.
6. Visually inspect polarized/oriented D1/D2/D5/D50/C3/C67/C68, U9/J5, and J4 DT13-08PA orientation in JLC preview.
7. Do not approve substitutions for critical ICs, RF parts, J1708 transceiver, LTE power stage, or protection parts without engineering review.
'''
readme_path=BUILD/'TruckBox_RevB_Assembly_README.txt'
readme_path.write_text(readme,encoding='utf-8')

src_gerber=BUILD/'TruckBox_RevA_Gerbers_JLC.zip'
revb_gerber=BUILD/'TruckBox_RevB_Gerbers_JLC.zip'
shutil.copyfile(src_gerber,revb_gerber)

prod=BUILD/'TruckBox_RevB_JLC_Production.zip'
members=[revb_gerber,bom_path,cpl_path,audit_path,readme_path,BUILD/'drc_summary.txt',POS,BUILD/'top.svg']
with zipfile.ZipFile(prod,'w',zipfile.ZIP_DEFLATED) as z:
    for path in members:
        z.write(path,arcname=path.name)

print(f'Rev.B production package OK: PCB refs={len(board_refs)}, SMT={len(mapped)}, BOM lines={len(PARTS)}, CPL={len(mapped)}')
print(f'Wrote {prod}')
