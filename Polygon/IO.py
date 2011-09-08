# -*- coding: utf-8 -*-
#       $Id: IO.py 107 2011-02-08 13:29:11Z jraedler $  

"""
This module provides functions for reading and writing Polygons in different
formats.

The following write-methods will accept different argument types for the 
output. If ofile is None, the method will create and return a StringIO-object. 
If ofile is a string, a file with that name will be created. If ofile is a 
file, it will be used for writing.

The following read-methods will accept different argument types for the 
output. An file or StringIO object will be used directly. If the argument is a 
string, the function tries to read a file with that name. If it fails, it 
will evaluate the string directly.
"""

from Polygon.cPolygon import Polygon
from io import StringIO

from xml.dom.minidom import parseString, Node

from struct import pack, unpack, calcsize

try:
    import reportlab
    hasPDFExport = True
except:
    hasPDFExport = False

try:
    import Imaging
    hasPILExport = True
except:
    hasPILExport = False


## some helpers

def __flatten(s):
    for a in s:
        for b in a:
            yield b


def __couples(s):
    for i in range(0, len(s), 2):
        yield s[i], s[i+1]


def __unpack(f, b):
    s = calcsize(f)
    return unpack(f, b[:s]), b[s:]
                        

class __RingBuffer:
    def __init__(self, seq):
        self.s = seq
        self.i = 0
        self.l = len(seq)
    def __call__(self):
        o = self.s[self.i]
        self.i += 1
        if self.i == self.l:
            self.i = 0
        return o


def getWritableObject(ofile):
    """try to make a writable file-like object from argument"""
    if ofile is None:
        return StringIO(), False
    elif type(ofile) == str:
        return open(ofile, 'w'), True
    elif type(ofile) in (file, StringIO):
        return ofile, False
    else:
        raise Exception("Can't make a writable object from argument!")


def getReadableObject(ifile):
    """try to make a readable file-like object from argument"""
    if type(ifile) == str:
        try:
            return open(ifile, 'r'), True
        except:
            return StringIO(ifile), True
    elif type(ifile) in (file, StringIO):
        return ifile, False
    else:
        raise Exception("Can't make a readable object from argument!")


def decodeBinary(bin):
    """
    Create Polygon from a binary string created with encodeBinary(). If the string 
    is not valid, the whole thing may break!

    :Arguments:
        - s: string
    :Returns:
        new Polygon
    """
    nC, b = __unpack('!I', bin)
    p = Polygon()
    for i in range(nC[0]):
        x, b = __unpack('!l', b)
        if x[0] < 0:
            isHole = 1
            s = -2*x[0]
        else:
            isHole = 0
            s = 2*x[0]
        flat, b = __unpack('!%dd' % s, b)
        p.addContour(tuple(__couples(flat)), isHole)
    return p


def encodeBinary(p):
    """
    Encode Polygon p to a binary string. The binary string will be in a standard 
    format with network byte order and should be rather machine independant. 
    There's no redundancy in the string, any damage will make the hole polygon 
    information unusable.

    :Arguments:
        - p: Polygon
    :Returns:
        string
    """
    l = [pack('!I', len(p))]
    for i, c in enumerate(p):
        l.append(pack('!l', len(c)*(1,-1)[p.isHole(i)]))
        l.append(pack('!%dd' %(2*len(c)), *__flatten(c)))
    return b"".join(l)


def writeGnuplot(ofile, polylist):
    """
    Write a list of Polygons to a gnuplot file, which may be plotted using the 
    command ``plot "ofile" with lines`` from gnuplot.

    :Arguments:
        - ofile: see above
        - polylist: sequence of Polygons
    :Returns:
        ofile object
    """
    f, cl = getWritableObject(ofile)
    for p in polylist:
        for vl in p:
            for j in vl:
                f.write('%g %g\n' % tuple(j))
            f.write('%g %g\n\n' % tuple(vl[0]))
    if cl: f.close()
    return f


def writeGnuplotTriangles(ofile, polylist):
    """
    Converts a list of Polygons to triangles and write the tringle data to a 
    gnuplot file, which may be plotted using the command 
    ``plot "ofile" with lines`` from gnuplot.

    :Arguments:
        - ofile: see above
        - polylist: sequence of Polygons
    :Returns:
        ofile object
    """
    f, cl = getWritableObject(ofile)
    for p in polylist:
        for vl in p.triStrip():
            j = 0
            for j in range(len(vl)-2):
                f.write('%g %g \n %g %g \n %g %g \n %g %g\n\n' %
                        tuple(vl[j]+vl[j+1]+vl[j+2]+vl[j]))
            f.write('\n')
    if cl: f.close()
    f.close()


def writeSVG(ofile, polylist, width=None, height=None, fill_color=None,
                fill_opacity=None, stroke_color=None, stroke_width=None):
    """
    Write a SVG representation of the Polygons in polylist, width and/or height 
    will be adapted if not given. fill_color, fill_opacity, stroke_color and 
    stroke_width can be sequences of the corresponding SVG style attributes to use.

    :Arguments:
        - ofile: see above
        - polylist: sequence of Polygons
        - optional width: float
        - optional height: height
        - optional fill_color: sequence of colors (3-tuples of floats: RGB)
        - optional fill_opacity: sequence of colors
        - optional stroke_color: sequence of colors
        - optional stroke_width: sequence of floats
    :Returns:
        ofile object
    """
    f, cl = getWritableObject(ofile)
    pp = [Polygon(p) for p in polylist] # use clones only
    [p.flop(0.0) for p in pp] # adopt to the SVG coordinate system 
    bbs = [p.boundingBox() for p in pp]
    bbs2 = list(zip(*bbs))
    minx = min(bbs2[0])
    maxx = max(bbs2[1])
    miny = min(bbs2[2])
    maxy = max(bbs2[3])
    xdim = maxx-minx
    ydim = maxy-miny
    if not (xdim or ydim):
        raise Error("Polygons have no extent in one direction!")
    a = ydim / xdim
    if not width and not height:
        if a < 1.0:
            width = 300
        else:
            height = 300
    if width and not height:
        height = width * a
    if height and not width:
        width = height / a
    npoly = len(pp)
    fill_color   = __RingBuffer(fill_color   or ('red', 'green', 'blue', 'yellow'))
    fill_opacity = __RingBuffer(fill_opacity or (1.0,))
    stroke_color = __RingBuffer(stroke_color or ('black',))
    stroke_width = __RingBuffer(stroke_width or (1.0,))
    s = ['<?xml version="1.0" encoding="iso-8859-1" standalone="no"?>',
         '<!DOCTYPE svg PUBLIC "-//W3C//DTD SVG 1.0//EN" "http://www.w3.org/TR/2001/REC-SVG-20010904/DTD/svg10.dtd">',
         '<svg xmlns="http://www.w3.org/2000/svg" width="%d" height="%d">' % (width, height)]
    for i in range(npoly):
        p = pp[i]
        bb = bbs[i]
        p.warpToBox(width*(bb[0]-minx)/xdim, width*(bb[1]-minx)/xdim,
                    height*(bb[2]-miny)/ydim, height*(bb[3]-miny)/ydim)
        subl = ['<path style="fill:%s;fill-opacity:%s;fill-rule:evenodd;stroke:%s;stroke-width:%s;" d="' %
                (fill_color(), fill_opacity(), stroke_color(), stroke_width())]
        for c in p:
            subl.append('M %g, %g %s z ' % (c[0][0], c[0][1], ' '.join([("L %g, %g" % (a,b)) for a,b in c[1:]])))
        subl.append('"/>')
        s.append(''.join(subl))
    s.append('</svg>')
    f.write('\n'.join(s))
    if cl: f.close()
    return f


def writeXML(ofile, polylist, withHeader=False):
    """
    Write a readable representation of the Polygons in polylist to a XML file. 
    A simple header can be added to make the file parsable.

    :Arguments:
        - ofile: see above
        - polylist: sequence of Polygons
        - optional withHeader: bool
    :Returns:
        ofile object
    """
    f, cl = getWritableObject(ofile)
    if withHeader:
        f.write('<?xml version="1.0" encoding="iso-8859-1" standalone="no"?>\n')
    for p in polylist:
        l = ['<polygon contours="%d" area="%g" xMin="%g" xMax="%g" yMin="%g" yMax="%g">' % ((len(p), p.area())+p.boundingBox())]
        for i, c in enumerate(p):
            l.append('  <contour points="%d" isHole="%d" area="%g" xMin="%g" xMax="%g" yMin="%g" yMax="%g">' \
                % ((len(c), p.isHole(i), p.area(i))+p.boundingBox(i)))
            for po in c:
                l.append('    <p x="%g" y="%g"/>' % po)
            l.append('  </contour>')
        l.append('</polygon>\n')
        f.write('\n'.join(l))
    if cl: f.close()
    return f


def readXML(ifile):
    """
    Read a list of Polygons from a XML file which was written with writeXML().
        
    :Arguments:
        - ofile: see above
    :Returns:
        list of Polygon objects
    """
    f, cl = getReadableObject(ifile)
    d = parseString(f.read())
    if cl: f.close()
    plist = []
    for pn in d.getElementsByTagName('polygon'):
        p = Polygon()
        plist.append(p)
        for sn in pn.childNodes:
            if not sn.nodeType == Node.ELEMENT_NODE:
                continue
            assert sn.tagName == 'contour'
            polist = []
            for pon in sn.childNodes:
                if not pon.nodeType == Node.ELEMENT_NODE:
                    continue
                polist.append((float(pon.getAttribute('x')), float(pon.getAttribute('y'))))
            assert int(sn.getAttribute('points')) == len(polist)
            p.addContour(polist, int(sn.getAttribute('isHole')))
        assert int(pn.getAttribute('contours')) == len(p)
    return plist


if hasPDFExport:

    def writePDF(ofile, polylist, pagesize=None, linewidth=0, fill_color=None):
        """
    *This function is only available if the reportlab package is installed!*
    Write a the Polygons in polylist to a PDF file.

    :Arguments:
        - ofile: see above
        - polylist: sequence of Polygons
        - optional pagesize: 2-tuple of floats
        - optional linewidth: float
        - optional fill_color: color
    :Returns:
        ofile object
    """
        from reportlab.pdfgen import canvas
        from reportlab.lib.colors import red, green, blue, yellow, black, white
        if not pagesize:
            from reportlab.lib.pagesizes import A4
            pagesize = A4
        can = canvas.Canvas(ofile, pagesize=pagesize)
        can.setLineWidth(linewidth)
        pp = [Polygon(p) for p in polylist] # use clones only
        bbs = [p.boundingBox() for p in pp]
        bbs2 = zip(*bbs)
        minx = min(bbs2[0])
        maxx = max(bbs2[1])
        miny = min(bbs2[2])
        maxy = max(bbs2[3])
        xdim = maxx-minx
        ydim = maxy-miny
        if not (xdim or ydim):
            raise Error("Polygons have no extent in one direction!")
        a = ydim / xdim
        width, height = pagesize
        if a > (height/width):
            width = height / a
        else:
            height = width * a
        npoly = len(pp)
        fill_color   = __RingBuffer(fill_color or (red, green, blue, yellow))
        for i in range(npoly):
            p = pp[i]
            bb = bbs[i]
            p.warpToBox(width*(bb[0]-minx)/xdim, width*(bb[1]-minx)/xdim,
                        height*(bb[2]-miny)/ydim, height*(bb[3]-miny)/ydim)
        for poly in pp:
            solids = [poly[i] for i in range(len(poly)) if poly.isSolid(i)]
            can.setFillColor(fill_color())
            for c in solids:
                p = can.beginPath()
                p.moveTo(c[0][0], c[0][1])
                for i in range(1, len(c)):
                    p.lineTo(c[i][0], c[i][1])
                p.close()
                can.drawPath(p, stroke=1, fill=1)
            holes = [poly[i] for i in range(len(poly)) if poly.isHole(i)]
            can.setFillColor(white)
            for c in holes:
                p = can.beginPath()
                p.moveTo(c[0][0], c[0][1])
                for i in range(1, len(c)):
                    p.lineTo(c[i][0], c[i][1])
                p.close()
                can.drawPath(p, stroke=1, fill=1)
        can.showPage()
        can.save()
