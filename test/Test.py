# -*- coding: utf-8 -*-
import Polygon
from Polygon.Shapes import Star, Circle, Rectangle
from Polygon.Utils import pointList, prunePoints, cloneGrid, gpfInfo
from Polygon.IO import writeGnuplot, writeSVG
import unittest
import sys

from operator import add
from math import fabs
from functools import reduce

aTri = lambda a,b,c: 0.5*fabs((b[0]-a[0])*(c[1]-a[1])-(c[0]-a[0])*(b[1]-a[1]))
cTri = lambda a,b,c: ((a[0]+b[0]+c[0])/3.0, (a[1]+b[1]+c[1])/3.0)
def tcenter(poly):
    a = []
    c = []
    for vl in poly.triStrip():
        for i in range(len(vl)-2):
            a.append(aTri(vl[i], vl[i+1], vl[i+2]))
            c.append(cTri(vl[i], vl[i+1], vl[i+2]))
    r = reduce(add, a)
    a = [x/r for x in a]
    ac = tuple(zip(a,c))
    return reduce(add, [i*j[0] for i, j in ac]),\
           reduce(add, [i*j[1] for i, j in ac])


def tarea(poly):
    a = []
    for vl in poly.triStrip():
        for i in range(len(vl)-2):
            a.append(aTri(vl[i], vl[i+1], vl[i+2]))
    return reduce(add, a)


def tboundingBox(poly):
    p = pointList(poly, 0)
    x = [a[0] for a in p]
    y = [a[1] for a in p]
    return min(x), max(x), min(y), max(y)


class PolygonTestCase(unittest.TestCase):
    
    def setUp(self):
        star   = Star(radius=2.0, center=(1.0, 3.0), beams=5, iradius=1.4)
        circle = Circle(radius=1.0, center=(1.0, 3.0), points=64)
        self.cookie = star-circle
        self.cont = [(0.0, 0.0), (2.0, 1.0), (1.0, 0.0), (-2.0, 1.0), (0.0, 0.0)]
        
    def testInit(self):
        Polygon.setDataStyle(Polygon.STYLE_TUPLE)
        # tuple
        p = Polygon.Polygon(self.cont)
        self.assertEqual(p[0], tuple(self.cont))
        # list
        p = Polygon.Polygon(self.cont)
        self.assertEqual(p[0], tuple(self.cont))
        if Polygon.withNumPy:
            import numpy
            a = numpy.array(self.cont)
            p = Polygon.Polygon(a)
            self.assertEqual(self.cont, list(p[0]))

    def testDataStyle(self):
        p = Polygon.Polygon(self.cont)
        # tuple
        Polygon.setDataStyle(Polygon.STYLE_TUPLE)
        self.assertEqual(p[0], tuple(self.cont))
        # list
        Polygon.setDataStyle(Polygon.STYLE_LIST)
        self.assertEqual(p[0], self.cont)
        if Polygon.withNumPy:
            import numpy
            Polygon.setDataStyle(Polygon.STYLE_NUMPY)
            self.assertEqual(type(p[0]), numpy.ndarray)

    def testPickle(self):
        import pickle
        p1 = self.cookie
        s = pickle.dumps(p1)
        p2 = pickle.loads(s)
        self.assertEqual(len(p1), len(p2))
        self.assertEqual(repr(p1), repr(p2))

    def testReadWrite(self):
        p1 = self.cookie
        p1.write('cookie.gpf')
        p2 = Polygon.Polygon()
        p2.read('cookie.gpf')
        self.assertEqual(len(p1), len(p2))
        self.assertEqual(repr(p1), repr(p2))

    def testClone(self):
        p1 = self.cookie
        p2 = Polygon.Polygon(p1)
        self.assertEqual(len(p1), len(p2))
        self.assertEqual(repr(p1), repr(p2))

    def testPointContainment(self):
        p = Polygon.Polygon(((0, 0), (0, 5), (5, 5), (5, 0)))
        p.addContour(((1, 1), (1, 4), (4, 4), (4, 1), (1, 1)), 1)
        p.addContour(((2, 2), (2, 3), (3, 3), (3, 2), (2, 2)), 2)
        self.assertEqual(p.isInside(2.5,-0.5), 0)
        self.assertEqual(p.isInside(2.5, 0.5), 1)
        self.assertEqual(p.isInside(2.5, 1.5), 0)
        self.assertEqual(p.isInside(2.5, 2.5), 1)
        self.assertEqual(p.isInside(2.5, 3.5), 0)
        self.assertEqual(p.isInside(2.5, 4.5), 1)
        self.assertEqual(p.isInside(2.5, 5.5), 0)
        self.assertEqual(p.isInside(2.5, 2.5, 0), 1)
        self.assertEqual(p.isInside(2.5, 2.5, 1), 1)
        self.assertEqual(p.isInside(2.5, 2.5, 2), 1)

    def testCoverOverlap(self):
        p1 = Star(radius=1.0, beams=6)
        p2 = Polygon.Polygon(p1)
        p2.scale(0.9, 0.9)
        self.assertEqual(p1.covers(p2), 1)
        self.assertEqual(p1.overlaps(p2), 1)
        p2.shift(0.2, 0.2)
        self.assertEqual(p1.covers(p2), 0)
        self.assertEqual(p1.overlaps(p2), 1)
        p2.shift(5.0, 0.0)
        self.assertEqual(p1.covers(p2), 0)
        self.assertEqual(p1.overlaps(p2), 0)

    def testCenterOfGravity(self):
        x, y   = self.cookie.center()
        tx, ty = tcenter(self.cookie)
        self.assertAlmostEqual(x, tx, places=10)
        self.assertAlmostEqual(y, ty, places=10)
        
    def testArea(self):
        a  = self.cookie.area()
        ta = tarea(self.cookie)
        self.assertAlmostEqual(a, ta, places=10)
        
    def testBoundingBox(self):
        bb   = self.cookie.boundingBox()
        tbb  = tboundingBox(self.cookie)
        self.assertEqual(bb, tbb)

    def testPrune(self):
        p1 = Polygon.Polygon(((0.0,0.0), (1.0,1.0), (2.0,2.0), \
                             (3.0,2.0), (3.0, 2.0), (3.0,1.0), \
                             (3.0,0.0), (2.0,0.0)))
        p2 = prunePoints(p1)
        self.assertAlmostEqual(p1.area(), p2.area(), places=10)

    def testLargeOperations(self):
        sheet = Rectangle(1000, 500)
        perf = cloneGrid(Circle(0.5, points=32), 0, 200, 100, 5, 5)
        perfSheet = sheet - perf
        circle = Circle(400, points=512)
        circle.scale(1.0, 0.5)
        circle.shift(500, 250)
        perfCircle = circle & perfSheet
        perfCircle.write('perfcircle.gpf')
        c, h, p, hf = gpfInfo('perfcircle.gpf')
        self.assertEqual(c, len(perfCircle))
        self.assertEqual(p, perfCircle.nPoints())
        self.assertEqual(hf, True)


if __name__ == '__main__':
    unittest.main()
