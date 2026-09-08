from pathlib import Path

OUT = Path(__file__).with_name('TruckBox_RevA.kicad_pcb')
W,H = 100.0,65.0
NETS = ['GND','BATT24_FUSED','VIN_PROT','BUCK_EN','BUCK_RT','BUCK_SS','SW_NODE','BOOT_SW','3V3_MAIN','FB_3V3','COMP','COMP_RC','PWRGD_TP','CORE_EN','CORE_BOOT','CAN1_H','CAN1_L','CAN1_RX','CAN1_TX_SAFE','CAN2_H','CAN2_L','CAN2_RX','CAN2_TX_SAFE','CAN_MODE','SPI_CLK','SPI_MOSI','SPI_MISO','FRAM_CS','FRAM_WP','FRAM_HOLD','3V3_SD','SD_CS','ACC_RAW','ACC_MID1','ACC_BASE','ACC_N','WDT_3V3','LINK_PWR_N','3V3_LINK','SD_PWR_N','WDT_REXT','WDT_DONE','WDT_WAKE_TP','CORE_TO_LINK','LINK_TO_CORE','LINK_EN','LINK_BOOT','GNSS_RX','GNSS_TX','GNSS_PPS','GNSS_ON','GNSS_RF','GNSS_VCC_RF','USB_D-_LINK_MCU','USB_D+_LINK_MCU','USB_D-_LINK','USB_D+_LINK','USB_CC1','USB_CC2','USB_VBUS']
net_id={name:i+1 for i,name in enumerate(NETS)}
def n(name): return net_id[name]
def q(s): return '"'+s.replace('"','\\"')+'"'
lines=[]
def emit(s=''): lines.append(s)

def pad(num,x,y,sx,sy,net=None,shape='roundrect',layers='"F.Cu" "F.Paste" "F.Mask"',drill=None,rot=0):
    if drill:
        sh='circle' if abs(sx-sy)<1e-6 else 'oval'
        return f'    (pad {q(str(num))} thru_hole {sh} (at {x:.3f} {y:.3f} {rot}) (size {sx:.3f} {sy:.3f}) (drill {drill}) (layers "*.Cu" "*.Mask")'+(f' (net {n(net)} {q(net)})' if net else '')+')'
    rr=' (roundrect_rratio 0.2)' if shape=='roundrect' else ''
    return f'    (pad {q(str(num))} smd {shape} (at {x:.3f} {y:.3f} {rot}) (size {sx:.3f} {sy:.3f}) (layers {layers}){rr}'+(f' (net {n(net)} {q(net)})' if net else '')+')'

def fp_header(name,ref,val,x,y,rot=0,attr='smd'):
    emit(f'  (footprint {q("TruckBox:"+name)} (layer "F.Cu")')
    emit(f'    (at {x:.3f} {y:.3f} {rot})')
    emit(f'    (attr {attr})')
    emit(f'    (fp_text reference {q(ref)} (at 0 -4 {rot}) (layer "F.SilkS") (effects (font (size 0.8 0.8) (thickness 0.12))))')
    emit(f'    (fp_text value {q(val)} (at 0 4 {rot}) (layer "F.Fab") (effects (font (size 0.7 0.7) (thickness 0.1))))')
def fp_end(): emit('  )')
def box(w,h):
    emit(f'    (fp_rect (start {-w/2:.3f} {-h/2:.3f}) (end {w/2:.3f} {h/2:.3f}) (stroke (width 0.12) (type solid)) (fill none) (layer "F.SilkS"))')
    emit(f'    (fp_rect (start {-w/2-0.25:.3f} {-h/2-0.25:.3f}) (end {w/2+0.25:.3f} {h/2+0.25:.3f}) (stroke (width 0.05) (type solid)) (fill none) (layer "F.CrtYd"))')

def fp_esp(ref,x,y,pn):
    fp_header('ESP32-C6-WROOM-1',ref,'ESP32-C6-WROOM-1-N8',x,y); box(18,25.5)
    left_y=[-8.26,-6.99,-5.72,-4.45,-3.18,-1.91,-0.64,0.63,1.90,3.17,4.44,5.71,6.98,8.25]
    right_y=[8.25,6.98,5.71,4.44,3.17,1.90,0.63,-0.64,-1.91,-3.18,-4.45,-5.72,-6.99,-8.26]
    for i,yy in enumerate(left_y,1): emit(pad(i,-8.75,yy,1.5,0.9,pn.get(i)))
    for i,yy in enumerate(right_y,15): emit(pad(i,8.75,yy,1.5,0.9,pn.get(i),rot=180))
    for xx in (-2.755,-1.505,-0.255):
        for yy in (-3.79,-2.54,-1.29): emit(pad(29,xx,yy,0.8,0.8,pn.get(29),rot=180))
    emit('    (zone (net 0) (net_name "") (layers "*.Cu") (name "antenna keepout") (hatch edge 0.508)')
    emit('      (connect_pads (clearance 0)) (min_thickness 0.254)')
    emit('      (keepout (tracks not_allowed) (vias not_allowed) (pads not_allowed) (copperpour not_allowed) (footprints not_allowed))')
    emit('      (fill (thermal_gap 0.508) (thermal_bridge_width 0.508))')
    emit('      (polygon (pts (xy 9 -9.75) (xy -9 -9.75) (xy -9 -15.75) (xy 9 -15.75))))')
    fp_end()

def fp_soic8(ref,val,x,y,pn):
    fp_header('SOIC-8_3.9x4.9_P1.27',ref,val,x,y); box(3.9,4.9); ys=[-1.905,-0.635,0.635,1.905]
    for i,yy in enumerate(ys,1): emit(pad(i,-2.7,yy,1.5,0.6,pn.get(i)))
    for i,yy in zip([8,7,6,5],ys): emit(pad(i,2.7,yy,1.5,0.6,pn.get(i)))
    fp_end()
def fp_sot23_3(ref,val,x,y,pn):
    fp_header('SOT-23-3',ref,val,x,y); box(2.9,2.5); emit(pad(1,-1.05,0.95,1.0,0.8,pn.get(1))); emit(pad(2,-1.05,-0.95,1.0,0.8,pn.get(2))); emit(pad(3,1.05,0,1.0,0.8,pn.get(3))); fp_end()
def fp_sc70_3(ref,val,x,y,pn):
    fp_header('SC70-3_DCK',ref,val,x,y); box(2.0,2.1); emit(pad(1,-1.0,0.65,0.8,0.6,pn.get(1))); emit(pad(2,-1.0,-0.65,0.8,0.6,pn.get(2))); emit(pad(3,1.0,0,0.8,0.6,pn.get(3))); fp_end()
def fp_sot23_6(ref,val,x,y,pn):
    fp_header('SOT-23-6_DDC',ref,val,x,y); box(2.9,2.8); ys=[0.95,0,-0.95]
    for i,yy in enumerate(ys,1): emit(pad(i,-1.1,yy,0.9,0.7,pn.get(i)))
    for i,yy in zip([6,5,4],ys): emit(pad(i,1.1,yy,0.9,0.7,pn.get(i)))
    fp_end()
def fp_hvssop10(ref,x,y,pn):
    fp_header('HVSSOP-10_DGQ_PowerPAD',ref,'TPS54260QDGQRQ1',x,y); box(3.0,3.0); ys=[-1.0,-0.5,0,0.5,1.0]
    for i,yy in enumerate(ys,1): emit(pad(i,-2.15,yy,1.4,0.28,pn.get(i),'rect'))
    for i,yy in zip([10,9,8,7,6],ys): emit(pad(i,2.15,yy,1.4,0.28,pn.get(i),'rect'))
    emit(pad(11,0,0,1.7,1.8,pn.get(11),'rect')); fp_end()
def fp_gnss(ref,x,y,pn):
    fp_header('ATGM336H-5NR32',ref,'ATGM336H-5NR32',x,y); box(12,12); right_y=[4.4,3.3,2.2,1.1,0,-1.1,-2.2,-3.3,-4.4]; left_y=[-4.4,-3.3,-2.2,-1.1,0,1.1,2.2,3.3,4.4]
    for i,yy in enumerate(right_y,1): emit(pad(i,4.85,yy,1.8,0.9,pn.get(i),'rect'))
    for i,yy in enumerate(left_y,10): emit(pad(i,-4.85,yy,1.8,0.9,pn.get(i),'rect'))
    fp_end()
def fp_ufl(ref,x,y,pn):
    fp_header('UFL_Hirose',ref,'U.FL-R-SMT-1',x,y); box(3.6,3.2); emit(pad(1,-1.05,0,1.05,1.0,pn.get(1),'rect')); emit(pad(2,0.475,1.475,2.2,1.05,pn.get(2),'rect')); emit(pad(2,0.475,-1.475,2.2,1.05,pn.get(2),'rect')); fp_end()
def fp_usbc(ref,x,y,rot,pn):
    fp_header('USB4105_compatible',ref,'USB4105-GF-A-060',x,y,rot); box(8.8,7.1)
    pads={'A1':(-3.3,-5.1,0.29),'A4':(-2.5,-5.1,0.29),'A5':(-1.25,-5.1,0.22),'A6':(-0.25,-5.1,0.22),'A7':(0.25,-5.1,0.22),'A8':(1.25,-5.1,0.22),'A9':(2.5,-5.1,0.29),'A12':(3.3,-5.1,0.29),'B1':(3.05,-5.1,0.25),'B4':(2.25,-5.1,0.25),'B5':(1.75,-5.1,0.22),'B6':(0.75,-5.1,0.22),'B7':(-0.75,-5.1,0.22),'B8':(-1.75,-5.1,0.22),'B9':(-2.25,-5.1,0.25),'B12':(-3.05,-5.1,0.25)}
    for k,(xx,yy,ww) in pads.items(): emit(pad(k,xx,yy,ww,1.35,pn.get(k),'rect'))
    for xx,yy,dh in [(-4.32,-4.18,'oval 0.6 1.7'),(-4.32,0,'oval 0.6 1.2'),(4.32,-4.18,'oval 0.6 1.7'),(4.32,0,'oval 0.6 1.2')]:
        emit(f'    (pad "Sh1" thru_hole oval (at {xx} {yy}) (size 1 2.2) (drill {dh}) (layers "*.Cu" "*.Mask")'+(f' (net {n(pn["Sh1"])} {q(pn["Sh1"])})' if pn.get('Sh1') else '')+')')
    fp_end()
def fp_microsd(ref,x,y,pn):
    fp_header('microSD_Molex_1040310811',ref,'1040310811',x,y); box(12.0,11.4); xs=[-3.105,-2.005,-0.905,0.195,1.295,2.395,3.495,4.545]
    for i,xx in enumerate(xs,1): emit(pad(i,xx,-5.45,0.85 if i<8 else 0.75,1.1,pn.get(i),'rect'))
    emit(pad(9,-5.74,0.7,1.2,1.0,pn.get(9),'rect')); emit(pad(10,-5.74,4.4,1.2,1.0,pn.get(10),'rect'))
    for xx,yy,sx,sy in [(5.755,-5.1,1.17,1.8),(-5.565,-5.325,1.55,1.35),(-2.24,5.375,1.9,1.35),(3.73,5.375,1.9,1.35)]: emit(pad(11,xx,yy,sx,sy,pn.get(11),'rect'))
    fp_end()
def fp_2(ref,val,x,y,net1,net2,kind='0603',rot=0):
    dims={'0603':(1.6,0.8,1.0,0.9),'0805':(2.0,1.25,1.2,1.3),'1210':(3.2,2.5,2.2,2.7),'SMA':(4.6,2.8,4.8,2.4),'SOD323':(1.7,1.25,2.1,0.8),'SOD128':(3.8,2.5,4.7,2.2)}; w,h,pitch,padh=dims[kind]
    fp_header(kind,ref,val,x,y,rot); box(w,h); emit(pad(1,-pitch/2,0,pitch*0.65,padh,net1)); emit(pad(2,pitch/2,0,pitch*0.65,padh,net2)); fp_end()
def fp_inductor(ref,x,y,net1,net2):
    fp_header('IND_7x7',ref,'18uH shielded',x,y); box(7,7); emit(pad(1,-3.0,0,2.2,4.5,net1)); emit(pad(2,3.0,0,2.2,4.5,net2)); fp_end()
def fp_tvs_do218(ref,x,y,pn):
    fp_header('DO218AB',ref,'SM8S33A',x,y); box(15.5,9.5); emit(pad(1,-6,0,5.0,7.0,pn.get(1))); emit(pad(2,6,0,5.0,7.0,pn.get(2))); fp_end()
def fp_j4(ref,x,y,pn):
    fp_header('PIGTAIL_2x4',ref,'Vehicle I/O 8-way',x,y,90,attr='through_hole'); box(8,14); coords=[(-1.8,-4.5),(1.8,-4.5),(-1.8,-1.5),(1.8,-1.5),(-1.8,1.5),(1.8,1.5),(-1.8,4.5),(1.8,4.5)]
    for i,(xx,yy) in enumerate(coords,1): emit(pad(i,xx,yy,2.2,2.2,pn.get(i),drill='1.2'))
    fp_end()
def fp_test(ref,x,y,net): fp_header('TP_1.5',ref,net,x,y); emit(pad(1,0,0,1.5,1.5,net,'circle')); fp_end()

emit('(kicad_pcb (version 20240108) (generator "TruckBox-generator-v1")'); emit('  (general (thickness 1.6))'); emit('  (paper "A4")'); emit('  (layers (0 "F.Cu" signal) (2 "In1.Cu" power "GND_PLANE") (4 "In2.Cu" signal "POWER_SIGNALS") (31 "B.Cu" signal) (36 "B.SilkS" user "B.Silkscreen") (37 "F.SilkS" user "F.Silkscreen") (44 "Edge.Cuts" user))'); emit('  (setup (pad_to_mask_clearance 0))'); emit('  (net 0 "")')
for name,i in net_id.items(): emit(f'  (net {i} {q(name)})')
for x1,y1,x2,y2 in [(0,0,W,0),(W,0,W,H),(W,H,0,H),(0,H,0,0)]: emit(f'  (gr_line (start {x1} {y1}) (end {x2} {y2}) (stroke (width 0.2) (type solid)) (layer "Edge.Cuts"))')
emit('  (gr_text "TruckBox Rev.A | 100x65 | 4L | PROTOTYPE" (at 50 63) (layer "F.SilkS") (effects (font (size 1 1) (thickness 0.15))))')
fp_esp('U1',18,16,{1:'GND',2:'3V3_MAIN',3:'CORE_EN',6:'SPI_CLK',7:'SPI_MOSI',8:'ACC_N',9:'WDT_DONE',10:'SPI_MISO',11:'FRAM_CS',15:'CORE_BOOT',16:'CAN1_RX',18:'CAN2_RX',20:'CORE_TO_LINK',21:'LINK_TO_CORE',24:'SD_PWR_N',25:'LINK_PWR_N',26:'SD_CS',27:'CAN_MODE',28:'GND',29:'GND'})
fp_esp('U2',82,16,{1:'GND',2:'3V3_LINK',3:'LINK_EN',13:'USB_D-_LINK_MCU',14:'USB_D+_LINK_MCU',15:'LINK_BOOT',16:'GNSS_PPS',20:'LINK_TO_CORE',21:'CORE_TO_LINK',24:'GNSS_RX',25:'GNSS_TX',28:'GND',29:'GND'})
fp_gnss('U5',50,16,{1:'GND',2:'GNSS_RX',3:'GNSS_TX',4:'GNSS_PPS',5:'GNSS_ON',6:'3V3_MAIN',8:'3V3_LINK',10:'GND',11:'GNSS_RF',12:'GND',14:'GNSS_VCC_RF'}); fp_ufl('J2',50,4,{1:'GNSS_RF',2:'GND'}); fp_soic8('U7','MB85RS64',31,36,{1:'FRAM_CS',2:'SPI_MISO',3:'FRAM_WP',4:'GND',5:'SPI_MOSI',6:'SPI_CLK',7:'FRAM_HOLD',8:'3V3_MAIN'}); fp_sot23_6('U8','TPL5010Q',24,38,{1:'WDT_3V3',2:'GND',3:'WDT_REXT',4:'WDT_DONE',5:'WDT_WAKE_TP',6:'CORE_EN'}); fp_microsd('J1',46,42,{2:'SD_CS',3:'SPI_MOSI',4:'3V3_SD',5:'SPI_CLK',6:'GND',7:'SPI_MISO',11:'GND'})
fp_soic8('U3','TCAN3404DRQ1',72,43,{1:'CAN1_TX_SAFE',2:'GND',3:'3V3_MAIN',4:'CAN1_RX',5:'CAN_MODE',6:'CAN1_L',7:'CAN1_H',8:'CAN_MODE'}); fp_soic8('U4','TCAN3404DRQ1',72,52,{1:'CAN2_TX_SAFE',2:'GND',3:'3V3_MAIN',4:'CAN2_RX',5:'CAN_MODE',6:'CAN2_L',7:'CAN2_H',8:'CAN_MODE'}); fp_sc70_3('D3','AQ24CANFD-02HTG',87,43,{1:'CAN1_H',2:'CAN1_L',3:'GND'}); fp_sc70_3('D4','AQ24CANFD-02HTG',87,52,{1:'CAN2_H',2:'CAN2_L',3:'GND'}); fp_j4('J4',96,48,{1:'BATT24_FUSED',2:'GND',3:'ACC_RAW',4:'CAN1_H',5:'CAN1_L',6:'CAN2_H',7:'CAN2_L'})
fp_usbc('J3',96,27,90,{'A1':'GND','A4':'USB_VBUS','A5':'USB_CC1','A6':'USB_D+_LINK','A7':'USB_D-_LINK','A9':'USB_VBUS','A12':'GND','B1':'GND','B4':'USB_VBUS','B5':'USB_CC2','B6':'USB_D+_LINK','B7':'USB_D-_LINK','B9':'USB_VBUS','B12':'GND','Sh1':'GND'}); fp_hvssop10('U6',61,58,{1:'BOOT_SW',2:'VIN_PROT',3:'BUCK_EN',4:'BUCK_SS',5:'BUCK_RT',6:'PWRGD_TP',7:'FB_3V3',8:'COMP',9:'GND',10:'SW_NODE',11:'GND'}); fp_tvs_do218('D1',84,59,{1:'GND',2:'VIN_PROT'}); fp_2('D2','STPST2H100AFY',74,59,'BATT24_FUSED','VIN_PROT','SOD128'); fp_2('D5','SS310AQ',63,52,'GND','SW_NODE','SMA',90); fp_inductor('L1',54,58,'3V3_MAIN','SW_NODE')
fp_sot23_3('Q1','AO3401A',67,28,{1:'LINK_PWR_N',2:'3V3_MAIN',3:'3V3_LINK'}); fp_sot23_3('Q2','AO3401A',53,34,{1:'SD_PWR_N',2:'3V3_MAIN',3:'3V3_SD'}); fp_sot23_3('Q3','MMBT5551',89,37,{1:'ACC_BASE',2:'GND',3:'ACC_N'}); fp_sot23_3('Q4','AO3401A',27,32,{1:'ACC_N',2:'3V3_MAIN',3:'WDT_3V3'}); fp_sc70_3('D6','TPD2E2U06Q',91,25,{1:'USB_D-_LINK',2:'USB_D+_LINK',3:'GND'}); fp_2('D7','1N4148WS',86,39,'GND','ACC_BASE','SOD323')
P=[('C1','2.2uF/100V',70,54,'VIN_PROT','GND','1210',0),('C2','2.2uF/100V',74,54,'VIN_PROT','GND','1210',0),('C3','47uF/63V',78,55,'VIN_PROT','GND','1210',90),('R31','681k',66.5,60.5,'VIN_PROT','BUCK_EN','0603',0),('R32','91k',64.5,61.5,'BUCK_EN','GND','0603',0),('R8','412k',59,62,'BUCK_RT','GND','0603',0),('C5','10nF',57,62,'BUCK_SS','GND','0603',0),('C4','100nF',61,54.5,'BOOT_SW','SW_NODE','0603',90),('C7','47uF/10V',49.5,54,'3V3_MAIN','GND','1210',0),('C8','47uF/10V',49.5,58,'3V3_MAIN','GND','1210',0),('R6','31.6k',57,56,'3V3_MAIN','FB_3V3','0603',90),('R7','10k',57,59,'FB_3V3','GND','0603',90),('R9','20k',63,60.5,'COMP','COMP_RC','0603',0),('C6','4.7nF',63,62,'COMP_RC','GND','0603',0),('R13','10k',26,20,'3V3_MAIN','CORE_EN','0603',0),('C19','1uF',27,22,'CORE_EN','GND','0603',0),('R29','10k',26,24,'3V3_MAIN','CORE_BOOT','0603',0),('R20','10k',29,26,'3V3_MAIN','CAN_MODE','0603',0),('R3','10k',68,40,'3V3_MAIN','CAN1_TX_SAFE','0603',0),('R4','10k',68,49,'3V3_MAIN','CAN2_TX_SAFE','0603',0),('R24','10k',34,31,'3V3_MAIN','FRAM_CS','0603',0),('R39','10k',28,31,'3V3_MAIN','FRAM_WP','0603',0),('R40','10k',31,31,'3V3_MAIN','FRAM_HOLD','0603',0),('R25','10k',51,39,'3V3_SD','SD_CS','0603',0),('R10','100k',92,35,'ACC_RAW','ACC_MID1','0603',90),('R28','100k',90.5,35,'ACC_MID1','ACC_BASE','0603',90),('R11','100k',86,36,'ACC_BASE','GND','0603',0),('R12','10k',85,34,'3V3_MAIN','ACC_N','0603',0),('C21','100nF',84,37,'ACC_N','GND','0603',90),('R26','100k',64,31,'3V3_MAIN','LINK_PWR_N','0603',0),('R27','100k',50,31,'3V3_MAIN','SD_PWR_N','0603',0),('R5','10.2k',21,35,'WDT_REXT','GND','0603',0),('R33','4.7k',42,27,'CORE_TO_LINK','CORE_TO_LINK','0603',0),('R34','4.7k',46,29,'LINK_TO_CORE','LINK_TO_CORE','0603',0),('R14','10k',74,28,'3V3_LINK','LINK_EN','0603',0),('C20','1uF',75,30,'LINK_EN','GND','0603',0),('R30','10k',88,22,'3V3_LINK','LINK_BOOT','0603',0),('R41','10k',56,20,'3V3_LINK','GNSS_ON','0603',0),('L2','47nH',44,16,'GNSS_VCC_RF','GNSS_RF','0603',0),('R1','5.1k',91,29,'USB_CC1','GND','0603',0),('R2','5.1k',91,31,'USB_CC2','GND','0603',0),('R42','22R',88,24,'USB_D-_LINK_MCU','USB_D-_LINK','0603',0),('R43','22R',88,26,'USB_D+_LINK_MCU','USB_D+_LINK','0603',0),('C9','100nF',27,14,'3V3_MAIN','GND','0603',0),('C10','100nF',73,14,'3V3_LINK','GND','0603',0),('C11','100nF',68,43,'3V3_MAIN','GND','0603',90),('C12','100nF',68,52,'3V3_MAIN','GND','0603',90),('C13','100nF',35,36,'3V3_MAIN','GND','0603',90),('C14','100nF',56,15,'3V3_LINK','GND','0603',90),('C15','10uF',27,16,'3V3_MAIN','GND','0805',0),('C16','10uF',73,16,'3V3_LINK','GND','0805',0),('C17','10uF',56,18,'3V3_LINK','GND','0805',0),('C18','10uF',53,42,'3V3_SD','GND','0805',90),('C24','100nF',28.5,38,'WDT_3V3','GND','0603',90)]
for ref,val,x,y,a,b,k,r in P: fp_2(ref,val,x,y,a,b,k,r)
fp_header('TACT_4x4','SW1','BOOT',91,20); box(4,4); emit(pad(1,-1.5,0,1.2,2.0,'LINK_BOOT')); emit(pad(2,1.5,0,1.2,2.0,'GND')); fp_end()
for ref,x,y,net in [('TP1',34,61,'3V3_MAIN'),('TP2',37,61,'GND'),('TP3',40,61,'CAN1_RX'),('TP4',43,61,'CAN2_RX'),('TP5',46,61,'CORE_EN'),('TP6',49,61,'PWRGD_TP'),('TP17',52,62,'PWRGD_TP')]: fp_test(ref,x,y,net)
emit(f'  (zone (net {n("GND")}) (net_name "GND") (layer "In1.Cu") (hatch edge 0.5) (connect_pads (clearance 0.25)) (min_thickness 0.25) (fill yes (thermal_gap 0.3) (thermal_bridge_width 0.3)) (polygon (pts (xy 0.5 0.5) (xy 99.5 0.5) (xy 99.5 64.5) (xy 0.5 64.5))))')
def seg(net,x1,y1,x2,y2,w=0.3,layer='F.Cu'): emit(f'  (segment (start {x1:.3f} {y1:.3f}) (end {x2:.3f} {y2:.3f}) (width {w:.3f}) (layer {q(layer)}) (net {n(net)}))')
seg('CAN1_H',94.2,49.5,87.0,43.65,0.35); seg('CAN1_H',87.0,43.65,74.7,42.365,0.35); seg('CAN1_L',97.8,49.5,87.0,42.35,0.35); seg('CAN1_L',87.0,42.35,74.7,43.635,0.35); seg('CAN2_H',94.2,52.5,87.0,52.65,0.35); seg('CAN2_H',87.0,52.65,74.7,51.365,0.35); seg('CAN2_L',97.8,52.5,87.0,51.35,0.35); seg('CAN2_L',87.0,51.35,74.7,52.635,0.35); seg('GNSS_RF',45.15,12.70,46.0,10.5,0.7); seg('GNSS_RF',46.0,10.5,48.95,4.0,0.7); seg('BATT24_FUSED',94.2,43.5,71.65,59.0,1.2); seg('VIN_PROT',76.35,59.0,58.85,57.5,1.2); seg('SW_NODE',63.15,57.0,57.0,58.0,1.0); seg('3V3_MAIN',51.0,58.0,48.0,58.0,1.0)
emit(')'); OUT.write_text('\n'.join(lines)+'\n',encoding='utf-8'); print(OUT)
