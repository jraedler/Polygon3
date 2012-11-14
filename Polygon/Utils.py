# -*- coding: utf-8 -*-

from Polygon.cPolygon import Polygon
from math import sqrt, fabs, floor
from operator import add
from functools import reduce
from collections import defaultdict


def fillHoles(poly):
    """
    Returns the polygon p without any holes.

    :Arguments:
        - p: Polygon
    :Returns:
        new Polygon
    """
    n = Polygon()
    [n.addContour(poly[i]) for i in range(len(poly)) if poly.isSolid(i)]
    return n


def pointList(poly, withHoles=1):
    """
    Returns a list of all points of p.

    :Arguments:
        - p: Polygon
    :Returns:
        list of points
    """
    if not withHoles:
        poly = fillHoles(poly)
    return reduce(add, [list(c) for c in poly])


def pointSet(poly, withHoles=1):
    """
    Returns a set of all points of p. Points are converted to tuples.

    :Arguments:
        - p: Polygon
    :Returns:
        set of points
    """
    if not withHoles:
        poly = fillHoles(poly)
    ps = set()
    for c in poly:
        ps.update(set(tuple(p) for p in c))
    return ps


__left = lambda p: (p[1][0]*p[2][1]+p[0][0]*p[1][1]+p[2][0]*p[0][1]-
                    p[1][0]*p[0][1]-p[2][0]*p[1][1]-p[0][0]*p[2][1] >= 0)
def convexHull(poly):
    """
    Returns a polygon which is the convex hull of p.

    :Arguments:
        - p: Polygon
    :Returns:
        new Polygon
    """
    points = list(pointSet(poly, 0))
    points.sort()
    u = [points[0], points[1]]
    for p in points[2:]:
        u.append(p)
        while len(u) > 2 and __left(u[-3:]):
            del u[-2]
    points.reverse()
    l = [points[0], points[1]]
    for p in points[2:]:
        l.append(p)
        while len(l) > 2 and __left(l[-3:]):
            del l[-2]
    return Polygon(u+l[1:-1])


def tile(poly, x=[], y=[], bb=None):
    """
    Returns a list of polygons which are tiles of p splitted at the border values 
    specified in x and y. If you already know the bounding box of p, you may give 
    it as argument bb (4-tuple) to speed up the calculation.

    :Arguments:
        - p: Polygon
        - x: list of floats
        - y: list of floats
        - optional bb: tuple of 4 floats
    :Returns:
        list of new Polygons
    """
    if not (x or y):
        return [poly] # nothin' to do
    bb = bb or poly.boundingBox()
    x = [bb[0]] + [i for i in x if bb[0] < i < bb[1]] + [bb[1]]
    y = [bb[2]] + [j for j in y if bb[2] < j < bb[3]] + [bb[3]]
    x.sort()
    y.sort()
    cutpoly = []
    for i in range(len(x)-1):
        for j in range(len(y)-1):
            cutpoly.append(Polygon(((x[i],y[j]),(x[i],y[j+1]),(x[i+1],y[j+1]),(x[i+1],y[j]))))
    tmp = [c & poly for c in cutpoly]
    return [p for p in tmp if p]


def tileEqual(poly, nx=1, ny=1, bb=None):
    """
    works like tile(), but splits into nx and ny parts.

    :Arguments:
        - p: Polygon
        - nx: integer
        - ny: integer
        - optional bb: tuple of 4 floats
    :Returns:
        list of new Polygons
    """
    bb = bb or poly.boundingBox()
    s0, s1 = bb[0], bb[2]
    a0, a1 = (bb[1]-bb[0])/nx, (bb[3]-bb[2])/ny 
    return tile(poly, [s0+a0*i for i in range(1, nx)], [s1+a1*i for i in range(1, ny)], bb)


def warpToOrigin(poly):
    """
    Shifts lower left corner of the bounding box to origin.

    :Arguments:
        - p: Polygon
    :Returns:
        None
    """
    x0, x1, y0, y1 = poly.boundingBox()
    poly.shift(-x0, -y0)


def centerAroundOrigin(poly):
    """
    Shifts the center of the bounding box to origin.

    :Arguments:
        - p: Polygon
    :Returns:
        None
    """
    x0, x1, y0, y1 = poly.boundingBox()
    poly.shift(-0.5*(x0+x1), -0.5*(y0+y1))


__vImp = lambda p: ((sqrt((p[1][0]-p[0][0])**2 + (p[1][1]-p[0][1])**2) +
                     sqrt((p[2][0]-p[1][0])**2 + (p[2][1]-p[1][1])**2)) *
                    fabs(p[1][0]*p[2][1]+p[0][0]*p[1][1]+p[2][0]*p[0][1]-
                              p[1][0]*p[0][1]-p[2][0]*p[1][1]-p[0][0]*p[2][1]))
def reducePoints(cont, n):
    """
    Remove points of the contour 'cont', return a new contour with 'n' points.
    *Very simple* approach to reduce the number of points of a contour. Each point 
    gets an associated 'value of importance' which is the product of the lengths 
    and absolute angle of the left and right vertex. The points are sorted by this 
    value and the n most important points are returned. This method may give 
    *very* bad results for some contours like symmetric figures. It may even 
    produce self-intersecting contours which are not valid to process with 
    this module.

    :Arguments:
        - cont: list of points
        - n: number of points to keep
    :Returns:
        new list of points
    """
    if n >= len(cont):
        return cont
    cont = list(cont)
    cont.insert(0, cont[-1])
    cont.append(cont[1])
    a = [(__vImp(cont[i-1:i+2]), i) for i in range(1, len(cont)-1)]
    a.sort()
    ind = [x[1] for x in a[len(cont)-n-2:]]
    ind.sort()
    return [cont[i] for i in ind]


def reducePointsDP(cont, tol): 
    """
    Remove points of the contour 'cont' using the Douglas-Peucker algorithm. The
    value of tol sets the maximum allowed difference between the contours. This 
    (slightly changed) code was written by Schuyler Erle and put into public
    domain. It uses an iterative approach that may need some time to complete, 
    but will give better results than reducePoints().

    :Arguments:
        - cont: list of points
        - tol: allowed difference between original and new contour
    :Returns:
        new list of points
    """
    anchor  = 0
    floater = len(cont) - 1
    stack   = []
    keep    = set()
    stack.append((anchor, floater))  
    while stack:
        anchor, floater = stack.pop()
        # initialize line segment
        # if cont[floater] != cont[anchor]:
        if cont[floater][0] != cont[anchor][0] or cont[floater][1] != cont[anchor][1]:
            anchorX = float(cont[floater][0] - cont[anchor][0])
            anchorY = float(cont[floater][1] - cont[anchor][1])
            seg_len = sqrt(anchorX ** 2 + anchorY ** 2)
            # get the unit vector
            anchorX /= seg_len
            anchorY /= seg_len
        else:
            anchorX = anchorY = seg_len = 0.0
        # inner loop:
        max_dist = 0.0
        farthest = anchor + 1
        for i in range(anchor + 1, floater):
            dist_to_seg = 0.0
            # compare to anchor
            vecX = float(cont[i][0] - cont[anchor][0])
            vecY = float(cont[i][1] - cont[anchor][1])
            seg_len = sqrt( vecX ** 2 + vecY ** 2 )
            # dot product:
            proj = vecX * anchorX + vecY * anchorY
            if proj < 0.0:
                dist_to_seg = seg_len
            else: 
                # compare to floater
                vecX = float(cont[i][0] - cont[floater][0])
                vecY = float(cont[i][1] - cont[floater][1])
                seg_len = sqrt( vecX ** 2 + vecY ** 2 )
                # dot product:
                proj = vecX * (-anchorX) + vecY * (-anchorY)
                if proj < 0.0:
                    dist_to_seg = seg_len
                else:  # calculate perpendicular distance to line (pythagorean theorem):
                    dist_to_seg = sqrt(abs(seg_len ** 2 - proj ** 2))
                if max_dist < dist_to_seg:
                    max_dist = dist_to_seg
                    farthest = i
        if max_dist <= tol: # use line segment
            keep.add(anchor)
            keep.add(floater)
        else:
            stack.append((anchor, farthest))
            stack.append((farthest, floater))
    keep = list(keep)
    keep.sort()
    return [cont[i] for i in keep]
    
    
__linVal = lambda p: (p[1][0]-p[0][0])*(p[2][1]-p[0][1])-(p[1][1]-p[0][1])*(p[2][0]-p[0][0])
def prunePoints(poly):
    """
    Returns a new Polygon which has exactly the same shape as p, but unneeded 
    points are removed. The new Polygon has no double points or points that are 
    exactly on a straight line.

    :Arguments:
        - p: Polygon
    :Returns:
        new Polygon
    """
    np = Polygon()
    for x in range(len(poly)): # loop over contours
        c = list(poly[x])
        c.insert(0, c[-1])
        c.append(c[1])
        # remove double points
        i = 1
        while (i < (len(c))):
            if c[i] == c[i-1]:
                del c[i]
            else:
                i += 1
        # remove points that are on a straight line
        n = []
        for i in range(1, len(c)-1):
            if __linVal(c[i-1:i+2]) != 0.0:
                n.append(c[i])
        if len(n) > 2:
            np.addContour(n, poly.isHole(x))
    return np
                

def cloneGrid(poly, con, xl, yl, xstep, ystep):
    """
    Create a single new polygon with contours that are made from contour con from 
    polygon poly arranged in a xl-yl-grid with spacing xstep and ystep.

    :Arguments:
        - poly: Polygon
        - con: integer
        - xl: integer
        - yl: integer
        - xstep: float
        - ystep: float
    :Returns:
        new Polygon
    """
    p = Polygon(poly[con])
    for xi in range(xl):
        for yi in range(yl):
            p.cloneContour(0, xi*xstep, yi*ystep)
    return p


#
# following functions are contributed by Josiah Carlson <josiah.carlson@gmail.com>
#
# the tileBSP() function is much faster for large polygons and a large number 
# of tiles than the original tile()
#
# background information can be found at:
#       http://dr-josiah.blogspot.com/2010/08/binary-space-partitions-and-you.html
# the original code is located here:
#       http://gist.github.com/560298
#

def _find_split(count_dict):
    '''
    When provided a dictionary of counts for the number of points inside each
    of the unit grid rows/columns, this function will return the best column
    choice so as to come closest to cutting the points in half.  It will also
    return the score, lower being better.

    Returns: (cutoff, score)
    '''
    # find the prefix sums
    tmp = {}
    for i in range(min(count_dict), max(count_dict)+1):
        tmp[i] = tmp.get(i-1, 0) + count_dict.get(i, 0)
    by_index = sorted(tmp.items())
    # the target number of points
    midpoint = by_index[-1][1] // 2
    # calculate how far off from the target number each choice would be
    totals = []
    for i in range(1, len(by_index)):
        totals.append(abs(by_index[i-1][1] - midpoint))
    # choose the best target number
    mi = min(totals)
    index = totals.index(mi)
    return by_index[index+1][0], totals[index]


def _single_poly(polygon, dim, maxv):
    for poly in polygon:
        if max(pt[dim] for pt in poly) > maxv:
            return False
    return True


def tileBSP(p):
    """
    This generator function returns tiles of a polygon. It will be much 
    more efficient for larger polygons and a large number of tiles than the 
    original tile() function. For a discussion see:
        http://dr-josiah.blogspot.com/2010/08/binary-space-partitions-and-you.html

    :Arguments:
        - p: Polygon
    :Returns:
        tiles of the Polygon p on the integer grid
    """
    _int = int
    _floor = floor

    work = [p]
    while work:
        # we'll use an explicit stack to ensure that degenerate polygons don't
        # blow the system recursion limit
        polygon = work.pop()

        # find out how many points are in each row/column of the grid
        xs = defaultdict(_int)
        ys = defaultdict(_int)
        for poly in polygon:
            for x,y in poly:
                xs[_int(_floor(x))] += 1
                ys[_int(_floor(y))] += 1

        # handle empty polygons gracefully
        if not xs:
            continue

        # handle top and right-edge border points
        mvx = max(max(x for x,y in poly) for poly in polygon)
        vx = _int(_floor(mvx))
        if len(xs) > 1 and mvx == vx:
            xs[vx-1] += xs.pop(vx, 0)
        mvy = max(max(y for x,y in poly) for poly in polygon)
        vy = _int(_floor(mvy))
        if len(ys) > 1 and mvy == vy:
            ys[vy-1] += ys.pop(vy, 0)

        # we've got a single grid, yield it
        if len(xs) == len(ys) == 1:
            yield polygon
            continue

        # find the split
        if len(xs) < 2:
            spx, countx = tuple(xs.items())[0]
            countx *= 3
        else:
            spx, countx = _find_split(xs)
        if len(ys) < 2:
            spy, county = tuple(ys.items())[0]
            county *= 3
        else:
            spy, county = _find_split(ys)

        # get the grid bounds for the split
        minx = min(xs)
        maxx = max(xs)
        miny = min(ys)
        maxy = max(ys)

        # actually split the polygon and put the results back on the work
        # stack
        if (countx < county and not _single_poly(polygon, 0, minx + 1.0)) or _single_poly(polygon, 1, miny + 1.0):
            work.append(polygon &
                Polygon([(minx, miny), (minx, maxy+1),
                         (spx, maxy+1), (spx, miny)]))
            work.append(polygon &
                Polygon([(spx, miny), (spx, maxy+1),
                         (maxx+1, maxy+1), (maxx+1, miny)]))
        else:
            work.append(polygon &
                Polygon([(minx, miny), (minx, spy),
                         (maxx+1, spy), (maxx+1, miny)]))
            work.append(polygon &
                Polygon([(minx, spy), (minx, maxy+1),
                         (maxx+1, maxy+1), (maxx+1, spy)]))

        # Always recurse on the smallest set, which is a trick to ensure that
        # the stack size is O(log n) .
        if work[-2].nPoints() < work[-1].nPoints():
            work.append(work.pop(-2))


def gpfInfo(fileName):
    """
    Get information on a gpc/gpf file.

    :Arguments:
        - fileName: name of the file to read
    :Returns:
        - contours: number of contours
        - holes: number of holes (if contained)
        - points: total number of points
        - withHoles: file contains hole-flags
    """
    f = open(fileName, 'r')
    contours = int(f.readline())
    holes  = 0
    points = 0
    withHoles = True
    for c in range(contours):
        pp = int(f.readline())
        x = 0
        if c == 0:
            # check for hole-flags
            try:
                holes += int(f.readline())
            except:
                withHoles = False
                x = 1
        else:
            if withHoles:
                holes += int(f.readline())
        [ f.readline() for p in range(pp-x) ]
        points += pp
    f.close()
    return contours, holes, points, withHoles
