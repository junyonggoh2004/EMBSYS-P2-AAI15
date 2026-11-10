from pathlib import Path
import re, html, textwrap
from reportlab.platypus import SimpleDocTemplate, Paragraph, Spacer, Table, TableStyle, PageBreak, KeepTogether
from reportlab.lib import colors
from reportlab.lib.styles import ParagraphStyle
from reportlab.lib.enums import TA_LEFT
from reportlab.pdfbase import pdfmetrics
from reportlab.pdfbase.ttfonts import TTFont
from reportlab.lib.pagesizes import A4

ROOT=Path('/Users/tanyilin/Downloads/EMBSYS-P2-AAI15')
FONT=Path('/System/Library/Fonts/Supplemental')
for name,file in [('Body','Arial.ttf'),('Strong','Arial Bold.ttf'),('Mono','Courier New.ttf')]:
    pdfmetrics.registerFont(TTFont(name,str(FONT/file)))
pdfmetrics.registerFontFamily('Body',normal='Body',bold='Strong',italic='Body',boldItalic='Strong')
INK=colors.HexColor('#172D43'); BLUE=colors.HexColor('#156A82'); PALE=colors.HexColor('#EEF4F7')
styles={
 'body':ParagraphStyle('body',fontName='Body',fontSize=9.5,leading=13,spaceAfter=6,textColor=INK),
 'title':ParagraphStyle('title',fontName='Strong',fontSize=23,leading=27,spaceAfter=14,textColor=INK),
 'heading':ParagraphStyle('heading',fontName='Strong',fontSize=17,leading=21,spaceAfter=13,textColor=BLUE),
 'code':ParagraphStyle('code',fontName='Mono',fontSize=8.2,leading=11,spaceAfter=0,textColor=INK,splitLongWords=True),
 'small':ParagraphStyle('small',fontName='Body',fontSize=8,leading=11,spaceAfter=8,textColor=BLUE),
 'cell':ParagraphStyle('cell',fontName='Body',fontSize=9,leading=12,textColor=INK),
}
def clean(s):
    return s.replace('\u2011','-').replace('\u2013','-').replace('\u2014','-')
def markup(s):
    s=html.escape(clean(s))
    s=re.sub(r'\[([^\]]+)\]\(([^)]+)\)',r'<a href="\2" color="#156A82">\1</a>',s)
    s=re.sub(r'`([^`]+)`',r'<font name="Mono" size="8.5">\1</font>',s)
    return re.sub(r'\*\*([^*]+)\*\*',r'<b>\1</b>',s)
story=[]
lines=(ROOT/'D3_HARDWARE_GUIDE.md').read_text().splitlines()
i=0; first_section=True
while i<len(lines):
    line=lines[i]
    if not line.strip(): i+=1;continue
    if line.startswith('# '):
        story.append(Paragraph('D3 hardware<br/>test guide',styles['title']))
        story.append(Paragraph('ESP32-S3 / one HC-SR04+ / LD2450<br/>Mac walkthrough - d3-board - 9 November 2026',styles['small']))
        story.append(Paragraph('READ BEFORE STARTING',styles['small']))
        i+=1;continue
    if line.startswith('## '):
        story.append(PageBreak())
        story.append(Paragraph(markup(line[3:]),styles['heading']))
        if first_section:
            story.append(Paragraph('Commands labelled as console commands go to the ESP32 monitor; shell commands go to Mac Terminal. Long commands wrap for printing: enter each BEGINCFG through ENDCFG as one continuous line, pressing Enter only at the end.',styles['small']))
            first_section=False
        i+=1;continue
    if line.startswith('```'):
        lang=line[3:]; block=[]; i+=1
        while i<len(lines) and not lines[i].startswith('```'):
            block.append(lines[i]);i+=1
        rows=[]
        for raw in block:
            if raw.startswith('BEGINCFG|'):
                chunks=[]; current=''
                for part in raw.split('|'):
                    candidate=current+('|' if current else '')+part
                    if len(candidate)>88:
                        chunks.append(current+'|');current=part
                    else:current=candidate
                chunks.append(current)
                rows.append(Paragraph('<br/>'.join(html.escape(clean(c)) for c in chunks),styles['code']))
            else:
                rows.append(Paragraph(html.escape(clean(raw)).replace(' ','&#160;'),styles['code']))
        label='BOARD CONSOLE' if lang=='text' else 'MAC TERMINAL'
        code=Table([[Paragraph(label,styles['small'])]]+[[p] for p in rows],colWidths=[479])
        code.setStyle(TableStyle([('BACKGROUND',(0,0),(-1,-1),PALE),('BOX',(0,0),(-1,-1),.5,colors.HexColor('#CEDCE5')),('LEFTPADDING',(0,0),(-1,-1),10),('RIGHTPADDING',(0,0),(-1,-1),10),('TOPPADDING',(0,0),(-1,0),8),('BOTTOMPADDING',(0,-1),(-1,-1),9),('TOPPADDING',(0,1),(-1,-1),2),('BOTTOMPADDING',(0,1),(-1,-2),2)]))
        story.extend([KeepTogether([code]),Spacer(1,10)]);i+=1;continue
    if line.startswith('|'):
        raw=[]
        while i<len(lines) and lines[i].startswith('|'):
            if not re.fullmatch(r'[| :\-]+',lines[i]):
                raw.append([Paragraph(markup(c.strip()),styles['cell']) for c in lines[i].strip('|').split('|')])
            i+=1
        widths=[170,309] if len(raw[0])==2 else [190,100,189]
        if len(raw[0])==3 and 'Check' in line: widths=[180,115,184]
        t=Table(raw,colWidths=widths,repeatRows=1,hAlign='LEFT')
        t.setStyle(TableStyle([('BACKGROUND',(0,0),(-1,0),colors.HexColor('#DDEBF0')),('VALIGN',(0,0),(-1,-1),'TOP'),('GRID',(0,0),(-1,-1),.4,colors.HexColor('#C5D4DD')),('TOPPADDING',(0,0),(-1,-1),5),('BOTTOMPADDING',(0,0),(-1,-1),5),('LEFTPADDING',(0,0),(-1,-1),8),('RIGHTPADDING',(0,0),(-1,-1),8)]))
        story.extend([t,Spacer(1,12)]);continue
    para=[line];i+=1
    while i<len(lines) and lines[i].strip() and not lines[i].startswith(('#','```','|')):
        if re.match(r'^\d+\. ',lines[i]):break
        para.append(lines[i]);i+=1
    story.append(Paragraph(markup(' '.join(para)),styles['body']))

def footer(canvas,doc):
    w,h=A4
    canvas.setStrokeColor(colors.HexColor('#CEDCE5'));canvas.line(58,43,w-58,43)
    canvas.setFont('Body',8);canvas.setFillColor(BLUE)
    canvas.drawString(58,30,'D3 HARDWARE WALKTHROUGH  |  Procedures - results pending')
    canvas.drawRightString(w-58,30,str(doc.page))
out=ROOT/'output/pdf/D3_Hardware_Test_Guide.pdf'
doc=SimpleDocTemplate(str(out),pagesize=A4,rightMargin=58,leftMargin=58,topMargin=44,bottomMargin=52,title='D3 Hardware Test Guide',author='D3 project',allowSplitting=1)
doc.build(story,onFirstPage=footer,onLaterPages=footer)
print(out)
