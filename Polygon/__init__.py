# -*- coding: utf-8 -*-


# import everything from cPolygon
from Polygon.cPolygon import *

# keep namespace clean
__version__ = version
__author__  = author
__license__ = license
del version, author, license


# support functions for pickling and unpickling
def __createPolygon(contour, hole):
    """rebuild Polygon from pickled data"""
    p = Polygon()
    for c, h in zip(contour, hole):
        p.addContour(c, h)
    return p


def __tuples(a):
    """map an array or list of lists to a tuple of tuples"""
    return tuple(tuple(i) for i in a)


def __reducePolygon(p):
    """return pickle data for Polygon """
    return (__createPolygon, (tuple([__tuples(x) for x in p]), p.isHole()))


import copyreg
copyreg.constructor(__createPolygon)
copyreg.pickle(Polygon, __reducePolygon, __createPolygon)
del copyreg
