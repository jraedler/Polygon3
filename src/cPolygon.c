#include <math.h>
#include <float.h>
#include <string.h>
#include <Python.h>
#include "gpc.h"
#include "PolyUtil.h"

#ifdef SYSTEM_WIN32
#include <malloc.h>
#endif /*SYSTEM_WIN32*/

#define POLY_AUTHOR \
"Author:   Joerg Raedler, Berlin. joerg@j-raedler.de\n\
Homepage: http://www.j-raedler.de/projects/polygon/\n\n\
Polygon is based on gpc, which was developed by Alan Murta, the gpc homepage\n\
is at: http://www.cs.man.ac.uk/~toby/alan/software/"

#define POLY_LICENSE \
"The Polygon package itself covered by the GNU LGPL, please look at \n\
http://www.gnu.org/copyleft/lesser.html for details.\n\n\
Polygon is based on GPC. GPC is free for non-commercial use only. \n\
We invite non-commercial users to make a voluntary donation towards the\n\
upkeep of GPC. If you wish to use GPC in support of a commercial product,\n\
you must obtain n official GPC Commercial Use Licence from The University\n\
of Manchester."

#define DOCSTR_POLY_MODULE \
"cPolygon - this module is part of the Polygon \n\
package. The most interesting thing here is a type/class called Polygon."

#define DOCSTR_POLY_TYPE \
"Polygon - a type to represent a polygon. In this module a polygon is a \n\
collection of contours, each contour may be normal or a hole inside \n\
other contours. \n\
\n\
The initialisation arguments may be:\n\
- another Polygon instance which will be cloned, or\n\
- the name of a file which will be read, or\n\
- a sequence of points which will be used as the first contour.\n\
A point is a sequence of two floats.\n\
\n\
Operations on polygons:\n\
:p & q:\n\
    intersection: a polygon with the area that is covered by both p and q\n\
:p | q:\n\
    union: a polygon containing the area that is covered by p or q or both\n\
:p - q:\n\
    difference: a polygon with the area of p that is not covered by q\n\
:p + q:\n\
    sum: same as union\n\
:p ^ q:\n\
    xor: a polygon with the area that is covered by exactly one of p and q\n\
:len(p):\n\
    number of contours\n\
:p[i]:\n\
    contour with index i, the same as p.contour(i), slicing is not yet \n\
    supported\n\
:bool(p):\n\
    logical value is true, if there are any contours in p (contours may be \n\
    empty!)\n"

#define STYLE_TUPLE 0
#define STYLE_LIST  1
#define STYLE_NUMPY 2

#ifndef DEFAULT_STYLE
#define DEFAULT_STYLE STYLE_LIST
#endif

#ifdef WITH_NUMPY
#define NPY_NO_DEPRECATED_API NPY_1_8_API_VERSION
#include <numpy/arrayobject.h>
#endif

#define STRBUF_MAX 300
#define DNDEF DBL_MAX
#define INDEF INT_MAX

#ifndef POLY_VERSION
#define POLY_VERSION "3.0.7"
#endif

#ifndef PyMODINIT_FUNC
#define PyMODINIT_FUNC DL_EXPORT(void)
#endif

extern double GPC_EPSILON;

static int dataStyle = DEFAULT_STYLE;

/* general Exception */
static PyObject *PolyError;

/* some common exceptions ... */
#define ERR_ARG PolyError, "Wrong number or type of arguments"
#define ERR_INV PolyError, "Invalid polygon or contour for operation"
#define ERR_IND PyExc_IndexError, "Index out of range for contour/strip"
#define ERR_MEM PyExc_MemoryError, "Out of memory"
#define ERR_TYP PyExc_TypeError, "Invalid type of operand"

/* raise an exception and return NULL */
static PyObject *Polygon_Raise(PyObject *e, char *msg) {
    PyErr_SetString(e, msg);
    return NULL;
}

/***************************  Polygon ********************************/

/* Polygon */
typedef struct {
    PyObject_HEAD
    PyObject *attr;
    gpc_polygon *gpc_p;
    double boundingBox[4];
    int bbValid;
} Polygon;

static PyTypeObject    Polygon_Type;
static PyMethodDef     Polygon_Methods[26];
static PyNumberMethods Polygon_NumberMethods;
static PySequenceMethods Polygon_SequenceMethods;

/* check if object o is a Polygon */
#define Polygon_Check(o) (PyObject_TypeCheck(o, &Polygon_Type))


/* gets (cached) boundingbox or calculates and caches */
static void Polygon_getBoundingBox(Polygon *p, double *x0, double *x1, double *y0, double *y1) {
    if (p->bbValid) {
        *x0 = p->boundingBox[0];
        *x1 = p->boundingBox[1];
        *y0 = p->boundingBox[2];
        *y1 = p->boundingBox[3];
    } else {
        poly_p_boundingbox(p->gpc_p, x0, x1, y0, y1);
        p->boundingBox[0] = *x0;
        p->boundingBox[1] = *x1;
        p->boundingBox[2] = *y0;
        p->boundingBox[3] = *y1;
        p->bbValid = 1;
    }
}


/* delete Polygon */
static void Polygon_dealloc(Polygon *self) {
    gpc_free_polygon(self->gpc_p);
    free(self->gpc_p);
    Py_XDECREF(self->attr);
    self->ob_base.ob_type->tp_free((PyObject*)self);
}


#define DOCSTR_POLY_WRITE \
"p.write(file)\n\
~~~~~~~~~~~~~\n\
Writes Polygon data to a file in gpc format.\n\
\n\
:Arguments:\n\
    - file: file name string\n\
    - optional holeflag: bool\n\
:Returns:\n\
    None\n"
static PyObject *Polygon_write(Polygon *self, PyObject *args) {
    PyObject *O;
    int hflag = 1;
    if (! PyArg_ParseTuple(args, "O|i", &O, &hflag))
        return Polygon_Raise(ERR_ARG);
    if (PyUnicode_Check(O)) {
        FILE *f = fopen(PyBytes_AsString(PyUnicode_AsEncodedString(O, Py_FileSystemDefaultEncoding, NULL)), "w");
        if (!f)
            return Polygon_Raise(PyExc_IOError, "Could not open file for writing!");
        gpc_write_polygon(f, hflag, self->gpc_p);
        fclose(f);
    } else
        return Polygon_Raise(ERR_ARG);
    Py_RETURN_NONE;
}


#define DOCSTR_POLY_READ \
"read(file)\n\
Reads Polygon data from a file in gpc format.\n\
:Arguments:\n\
    - file: file name string\n\
    - optional holeflag: bool\n\
:Returns:\n\
    None\n\
"
static PyObject *Polygon_read(Polygon *self, PyObject *args) {
    PyObject *O;
    int hflag = 1;
    if (! PyArg_ParseTuple(args, "O|i", &O, &hflag))
        return Polygon_Raise(ERR_ARG);
    if (PyUnicode_Check(O)) {
        FILE *f = fopen(PyBytes_AsString(PyUnicode_AsEncodedString(O, Py_FileSystemDefaultEncoding, NULL)), "r");
        if (!f)
            return Polygon_Raise(PyExc_IOError, "Could not open file for reading!");
        gpc_read_polygon(f, hflag, self->gpc_p);
        fclose(f);
    } else
        return Polygon_Raise(ERR_ARG);
    Py_RETURN_NONE;
}


#define DOCSTR_POLY_ADDCONTOUR \
"addContour(c |, hole=0)\n\
Add a contour (outline or hole).\n\
:Arguments:\n\
    - c: pointlist (sequence of 2-tuples)\n\
    - optional hole: bool\n\
:Returns:\n\
    None\n"
static PyObject *Polygon_addContour(Polygon *self, PyObject *args) {
#ifdef WITH_NUMPY
    PyObject *o=NULL;
    PyArrayObject *a=NULL;
    gpc_vertex_list *vl;
    int hole = 0;
    if (! PyArg_ParseTuple(args, "O|i", &o, &hole))
        return Polygon_Raise(ERR_ARG);
    if ((a = (PyArrayObject *)PyArray_ContiguousFromObject(o, NPY_DOUBLE, 2, 2)) == NULL)
        return Polygon_Raise(ERR_ARG);
    if (PyArray_NDIM(a) != 2) return Polygon_Raise(ERR_ARG);
    if (PyArray_DIMS(a)[1] != 2) return Polygon_Raise(ERR_ARG);
    vl = PyMem_New(gpc_vertex_list, 1);
    vl->num_vertices = PyArray_DIMS(a)[0];
    vl->vertex = PyMem_New(gpc_vertex, vl->num_vertices);
    memcpy((vl->vertex), PyArray_DATA(a), 2*vl->num_vertices*sizeof(double));
    Py_DECREF(a);
#else
    PyObject *list=NULL, *flist, *point=NULL, *X, *Y;
    gpc_vertex_list *vl;
    gpc_vertex *v;
    int i, imax, hole = 0;
    if (! PyArg_ParseTuple(args, "O|i", &list, &hole))
        return Polygon_Raise(ERR_ARG);
    if (! PySequence_Check(list))
        return Polygon_Raise(ERR_ARG);
    flist = PySequence_Fast(list, "this is not a sequence");
    if ((! flist) || ((imax = PySequence_Length(flist)) <= 2))
        return Polygon_Raise(ERR_INV);
    vl = PyMem_New(gpc_vertex_list, 1);
    vl->num_vertices = imax;
    vl->vertex = v = PyMem_New(gpc_vertex, imax);
    for (i=0; i<imax; i++) {
        point = PySequence_Fast(PySequence_Fast_GET_ITEM(flist, i), "this is not a point");
        if ((!point) || (PySequence_Length(point) != 2))
            return Polygon_Raise(ERR_INV);
        v->x = PyFloat_AsDouble(X = PyNumber_Float(PySequence_Fast_GET_ITEM(point, 0)));
        v->y = PyFloat_AsDouble(Y = PyNumber_Float(PySequence_Fast_GET_ITEM(point, 1)));
        v++;
        Py_DECREF(X);
        Py_DECREF(Y);
        Py_DECREF(point);
    }
    Py_DECREF(flist);
#endif /* WITH_NUMPY */
    gpc_add_contour(self->gpc_p, vl, hole);
    self->bbValid = 0;
    PyMem_Free(vl->vertex);
    PyMem_Free(vl);
    Py_RETURN_NONE;
}


/* make a new Polygon, using gpc_p if not NULL */
static Polygon *Polygon_NEW(gpc_polygon *gpc_p) {
    Polygon *obj = PyObject_NEW(Polygon, &Polygon_Type);
    if (gpc_p != NULL)
        obj->gpc_p = gpc_p;
    else {
        if (! (obj->gpc_p = poly_p_new()))
            return (Polygon *)Polygon_Raise(ERR_MEM);
    }
    obj->bbValid = 0;
    obj->attr = NULL;
    return obj;
}


static PyObject *Polygon_new(PyTypeObject *type, PyObject *args, PyObject *kwds) {
    Polygon *self;
    self = (Polygon *)type->tp_alloc(type, 0);
    if (self != NULL) {
        self->bbValid = 0;
        if (! (self->gpc_p = poly_p_new())) {
            Py_DECREF(self);
            return NULL;
        }
    }
    return (PyObject *)self;
}


static int Polygon_init(Polygon *self, PyObject *args, PyObject *kwds) {
    PyObject *O = NULL, *TMP = NULL;
    int hole;
    static char *kwlist[] = {"contour", "hole", NULL};
    if (! PyArg_ParseTupleAndKeywords(args, kwds, "|Oi", kwlist, &O, &hole))
        return -1; 
    if (O != NULL) {
        if ((PyTypeObject *)PyObject_Type(O) == &Polygon_Type) {
            if (poly_p_clone(((Polygon *)O)->gpc_p, self->gpc_p) != 0) {
                Polygon_dealloc(self);
                Polygon_Raise(ERR_MEM);
                return -1;
            }
        } else if (PyUnicode_Check(O)) {
            TMP = Polygon_read(self, args);
        } else if (PySequence_Check(O)) {
            TMP = Polygon_addContour(self, args);
        } else {
            Polygon_Raise(ERR_ARG);
            return -1;
        }
        if (PyErr_Occurred()) {
            return -1;
        }
        if (TMP) {
            Py_DECREF(TMP);
        }
    }
    return 0;
}


static PyObject *Polygon_repr(PyObject *self) {
    char buf[STRBUF_MAX];
    int i, j;
    gpc_polygon * p = ((Polygon *)self)->gpc_p;
    gpc_vertex_list *vl;
    gpc_vertex *v;
    PyObject * s = PyBytes_FromString("Polygon:");
    vl = p->contour;
    for(i=0; i<p->num_contours; i++) {
        if (*((p->hole)+i) == 0) 
            sprintf(buf, "\n  <%d:Contour:", i);
        else
            sprintf(buf, "\n  <%d:Hole   :", i);
        PyBytes_ConcatAndDel(&s, PyBytes_FromString(buf));
        v = vl->vertex;
        for (j = 0; j < vl->num_vertices; j++) {
            sprintf(buf, " [%d:%#.2g, %#.2g]", j, v->x, v->y);
            PyBytes_ConcatAndDel(&s, PyBytes_FromString(buf));
            v++;
        }
        PyBytes_ConcatAndDel(&s, PyBytes_FromString(">"));
        vl++;
    }
    return PyUnicode_FromEncodedObject(s, "ascii", "strict");
}


static Py_ssize_t Polygon_len(PyObject *self) {
    return ((Polygon *)self)->gpc_p->num_contours;
}


static PyObject *Polygon_getitem(PyObject *self, Py_ssize_t item) {
    PyObject *R;
    gpc_vertex_list * vl = NULL;
    gpc_vertex *v;
    int i, imax;
    gpc_polygon *p = ((Polygon *)self)->gpc_p;
    if (item < 0) item += p->num_contours;
    if ((item >= p->num_contours) || (item < 0))
        return Polygon_Raise(ERR_IND);
    vl = (p->contour)+item;
    imax = vl->num_vertices;
    switch (dataStyle) {
        case STYLE_TUPLE: {
            PyObject *XY;
            v = vl->vertex;
            R = PyTuple_New(imax);
            for (i=0; i < imax; i++) {
                XY = PyTuple_New(2);
                PyTuple_SetItem(XY, 0, PyFloat_FromDouble(v->x));
                PyTuple_SetItem(XY, 1, PyFloat_FromDouble(v->y));
                PyTuple_SetItem(R, i, XY);
                v++;
            }
        } break;
        case STYLE_LIST: {
            PyObject *XY;
            v = vl->vertex;
            R = PyList_New(imax);
            for (i=0; i < imax; i++) {
                XY = PyTuple_New(2);
                PyTuple_SetItem(XY, 0, PyFloat_FromDouble(v->x));
                PyTuple_SetItem(XY, 1, PyFloat_FromDouble(v->y));
                PyList_SetItem(R, i, XY);
                v++;
            }
        } break;
#ifdef WITH_NUMPY
        case STYLE_NUMPY: {
            npy_intp dims[2] = {0, 2};
            dims[0] = imax; 
            R = PyArray_SimpleNew(2, dims, NPY_DOUBLE);
            memcpy(PyArray_DATA((PyArrayObject *)R), vl->vertex, sizeof(gpc_vertex)*vl->num_vertices);
        } break;
#endif /* WITH_NUMPY */
        default:
            return Polygon_Raise(PolyError, "Unknown data style");
    }
    return R;
}


#define DOCSTR_POLY_CONTOUR \
"contour(i)\n\
gives the contour with index i (the same as p[i])\n\
:Arguments:\n\
    - i: integer\n\
:Returns:\n\
    a contour\n"
static PyObject *Polygon_getContour(Polygon *self, PyObject *args) {
    int item;
    if (!PyArg_ParseTuple(args, "i", &item))
        return Polygon_Raise(ERR_ARG);
    return Polygon_getitem((PyObject *)self, item);
}


#define DOCSTR_POLY_SIMPLIFY \
"simplify()\n\
Try to simplify Polygon. It's possible to add overlapping contours or holes\n\
which are outside of other contours. This may result in wrong calculations of \n\
the area, center point, bounding box or other values. Call this method to \n\
make sure the Polygon is in a good shape. The method first adds all contours \n\
with a hole flag of 0, then substracts all holes and replaces  the original \n\
Polygon with the result.\n\
:Arguments:\n\
    None\n\
:Returns:\n\
    None\n"
static PyObject *Polygon_simplify(Polygon *self, PyObject *args) {
    gpc_polygon *ret, *lop, *rop, *tmp, *p = self->gpc_p;
    int i;
    if (p->num_contours <= 0) {
        Py_RETURN_NONE;
    }
    if (! (lop = poly_p_new())) return Polygon_Raise(ERR_MEM);
    if (! (rop = poly_p_new())) return Polygon_Raise(ERR_MEM);
    if (! (ret = poly_p_new())) return Polygon_Raise(ERR_MEM);
    /* find first contour which is not a hole */
    i = 0;
    while ((i < p->num_contours) && (p->hole[i] == 1))
        i++;
    if (i < p->num_contours) 
        gpc_add_contour(lop, p->contour+i, 0);
    /* then try to add other contours */
    for (i++; i < p->num_contours; i++) {
        if (p->hole[i] == 0) {
            gpc_free_polygon(rop);
            gpc_free_polygon(ret);
            gpc_add_contour(rop, (p->contour+i), 0);
            gpc_polygon_clip(GPC_UNION, lop, rop, ret); 
            tmp = lop;
            lop = ret;
            ret = tmp;
        }
    }
    /* then try to cut out holes */
    for (i = 0; i < p->num_contours; i++) {
        if (p->hole[i] == 1) {
            gpc_free_polygon(rop);
            gpc_free_polygon(ret);
            gpc_add_contour(rop, (p->contour+i), 0);
            gpc_polygon_clip(GPC_DIFF, lop, rop, ret); 
            tmp = lop;
            lop = ret;
            ret = tmp;
        }
    }
    gpc_free_polygon(self->gpc_p);
    free(self->gpc_p);
    self->gpc_p = lop;
    gpc_free_polygon(ret);
    free(ret);
    gpc_free_polygon(rop);
    free(rop);
    self->bbValid = 0;
    Py_RETURN_NONE;
}


#define DOCSTR_POLY_SHIFT \
"shift(xs, ys)\n\
Shifts the polygon by adding xs and ys.\n\
:Arguments:\n\
    - xs: float\n\
    - ys float\n\
:Returns:\n\
    None\n"
static PyObject *Polygon_shift(Polygon *self, PyObject *args) {
    double x, y;
    if (! PyArg_ParseTuple(args, "dd", &x, &y))
        return Polygon_Raise(ERR_ARG);
    if ((x != 0.0) || (y != 0.0))
        poly_p_shift(self->gpc_p, x, y);
    self->bbValid = 0;
    Py_RETURN_NONE;
}


#define DOCSTR_POLY_SCALE \
"scale(xs, ys |, xc, yc)\n\
Scales the polygon by multiplying with xs and ys around the center point. If\n\
no center is given the center point of the bounding box is used, which will\n\
not be changed by this operation.\n\
:Arguments:\n\
    - xs: float\n\
    - ys: float\n\
    - optional xc: float\n\
    - optional yc: float\n\
:Returns:\n\
    None\n"
static PyObject *Polygon_scale(Polygon *self, PyObject *args) {
    double xs, ys, xc=DNDEF, yc=DNDEF;
    if (! PyArg_ParseTuple(args, "dd|dd", &xs, &ys, &xc, &yc))
        return Polygon_Raise(ERR_ARG);
    if ((xs != 1.0) || (ys != 1.0)) {
        if (xc == DNDEF) {
            double x0, x1, y0, y1;
            Polygon_getBoundingBox(self, &x0, &x1, &y0, &y1);
            xc = 0.5 * (x0+x1);
            yc = 0.5 * (y0+y1);
        }
        poly_p_scale(self->gpc_p, xs, ys, xc, yc);
    }
    self->bbValid = 0;
    Py_RETURN_NONE;
}


#define DOCSTR_POLY_CLONE_CONTOUR \
"cloneContour(i |, xs, ys)\n\
Clones the contour i, returns index of clone, optionally shifts clone by xs \n\
and ys.\n\
:Arguments:\n\
    - i: integer\n\
    - optional xs: float\n\
    - optional ys: float\n\
:Returns:\n\
    integer\n"
static PyObject *Polygon_cloneContour(Polygon *self, PyObject *args) {
    int con, i, hole=-1;
    double xs=1.0, ys=1.0;
    gpc_vertex_list *vl = NULL, *vlnew = NULL;
    gpc_polygon *p = ((Polygon *)self)->gpc_p;
    if (! PyArg_ParseTuple(args, "i|ddi", &con, &xs, &ys, &hole))
        return Polygon_Raise(ERR_ARG);
    if (con < 0) con += p->num_contours;
    if ((con >= p->num_contours) || (con < 0))
        return Polygon_Raise(ERR_IND);
    vl = (p->contour)+con;
    vlnew = PyMem_New(gpc_vertex_list, 1);
    vlnew->num_vertices = vl->num_vertices;
    vlnew->vertex = PyMem_New(gpc_vertex, vlnew->num_vertices);
    for (i=0; i < vl->num_vertices; i++) {
        vlnew->vertex[i].x = vl->vertex[i].x + xs;
        vlnew->vertex[i].y = vl->vertex[i].y + ys;
    }
    gpc_add_contour(p, vlnew, p->hole[con]);
    self->bbValid = 0;
    PyMem_Free(vlnew->vertex);
    PyMem_Free(vlnew);
    return Py_BuildValue("i", (p->num_contours)-1);
}


#define DOCSTR_POLY_ROTATE \
"rotate(a |, xc, yc)\n\
Rotates the polygon by angle a around center point in ccw direction. If no \n\
center is given the center point of the bounding box is used.\n\
:Arguments:\n\
    - a: float\n\
    - optional xc: float\n\
    - optional yc: float\n\
:Returns:\n\
    None\n"
static PyObject *Polygon_rotate(Polygon *self, PyObject *args) {
    double alpha, xc=DNDEF, yc=DNDEF;
    if (! PyArg_ParseTuple(args, "d|dd", &alpha, &xc, &yc))
        return Polygon_Raise(ERR_ARG);
    if (alpha != 0.0) {
        if (xc == DNDEF) {
            double x0, x1, y0, y1;
            Polygon_getBoundingBox(self, &x0, &x1, &y0, &y1);
            xc = 0.5 * (x0+x1);
            yc = 0.5 * (y0+y1);
        }
        poly_p_rotate(self->gpc_p, alpha, xc, yc);
    }
    self->bbValid = 0;
    Py_RETURN_NONE;
}


#define DOCSTR_POLY_WARPTOBOX \
"warpToBox(x0, x1, y0, y1)\n\
Scales and shifts the polygon to fit into the bounding box specified by x0, \n\
x1, y0 and y1. Make sure: x0 < x1 and y0 < y1!\n\
:Arguments:\n\
    - x0: float\n\
    - x1: float\n\
    - y0: float\n\
    - y1: float\n\
:Returns:\n\
    None\n"
static PyObject *Polygon_warpToBox(Polygon *self, PyObject *args) {
    double x0, x1, y0, y1;
    if (! PyArg_ParseTuple(args, "dddd", &x0, &x1, &y0, &y1))
        return Polygon_Raise(ERR_ARG);
    if (self->bbValid)
        poly_p_warpToBox(self->gpc_p, x0, x1, y0, y1, self->boundingBox);
    else
        poly_p_warpToBox(self->gpc_p, x0, x1, y0, y1, NULL);
    self->bbValid = 0;
    Py_RETURN_NONE;
}


#define DOCSTR_POLY_FLIP \
"flip(|x)\n\
Flips polygon in x direction. If a value for x is not given, the center of the \n\
bounding box is used.\n\
:Arguments:\n\
    - optional x: float\n\
:Returns:\n\
    None\n"
static PyObject *Polygon_flip(Polygon *self, PyObject *args) {
    double x = DNDEF;
    if (! PyArg_ParseTuple(args, "|d", &x))
        return Polygon_Raise(ERR_ARG);
    if (x == DNDEF) {
        double x0, x1, y0, y1;
        Polygon_getBoundingBox(self, &x0, &x1, &y0, &y1);
        x = 0.5 * (x0+x1);
    } else
        self->bbValid = 0;
    poly_p_flip(self->gpc_p, x);
    Py_RETURN_NONE;
}


#define DOCSTR_POLY_FLOP \
"flop(|y)\n\
Flips polygon in y direction. If a value for y is not given, the center of the \n\
bounding box is used.\n\
:Arguments:\n\
    - optional y: float\n\
:Returns:\n\
    None\n"
static PyObject *Polygon_flop(Polygon *self, PyObject *args) {
    double y = DNDEF;
    if (! PyArg_ParseTuple(args, "|d", &y))
        return Polygon_Raise(ERR_ARG);
    if (y == DNDEF) {
        double x0, x1, y0, y1;
        Polygon_getBoundingBox(self, &x0, &x1, &y0, &y1);
        y = 0.5 * (y0+y1);
    } else
        self->bbValid = 0;
    poly_p_flop(self->gpc_p, y);
    Py_RETURN_NONE;
}


#define DOCSTR_POLY_NPOINTS \
"nPoints(|i)\n\
Returns the number of points of one contour or of the whole polygon. Is much \n\
faster than len(p[i]) or reduce(add, map(len, p))!\n\
:Arguments:\n\
    - optional i: integer\n\
:Returns:\n\
    integer\n"
static PyObject *Polygon_nPoints(Polygon *self, PyObject *args) {
    int i=INDEF, n=0;
    if (! PyArg_ParseTuple(args, "|i", &i))
        return Polygon_Raise(ERR_ARG);
    if (i!=INDEF) {
        if ((i >= 0) && (i < self->gpc_p->num_contours))
            return Py_BuildValue("i",  self->gpc_p->contour[i].num_vertices);
        else 
            return Polygon_Raise(ERR_IND);
    }
    for (i=0; i < self->gpc_p->num_contours; i++) n += self->gpc_p->contour[i].num_vertices;
    return Py_BuildValue("i", n);
}


#define DOCSTR_POLY_AREA \
"area(|i)\n\
Calculates the area of one contour (when called with index) or of the whole \n\
polygon. All values are positive! The polygon area is the sum of areas of all \n\
solid contours minus the sum of all areas of holes.\n\
:Arguments:\n\
    - optional i: integer\n\
:Returns:\n\
    a float\n"
static PyObject *Polygon_area(Polygon *self, PyObject *args) {
    int i=INDEF;
    if (! PyArg_ParseTuple(args, "|i", &i))
        return Polygon_Raise(ERR_ARG);
    if (i!=INDEF) {
        if ((i >= 0) && (i < self->gpc_p->num_contours))
            return Py_BuildValue("d", poly_c_area(self->gpc_p->contour+i));
        else 
            return Polygon_Raise(ERR_IND);
    }
    return Py_BuildValue("d",  poly_p_area(self->gpc_p));
}


#define DOCSTR_POLY_ORIENTATION \
"orientation(|i)\n\
Calculates the orientation of one contour (when called with index) or of all \n\
contours. There's no default orientation, holes are defined by the hole flag, \n\
not by the orientation!\n\
:Arguments:\n\
    - optional i: integer\n\
:Returns:\n\
    single integer or list of integers: 1 for ccw,  -1 for cw, 0 for invalid \n\
    contour.\n"
static PyObject *Polygon_orientation(Polygon *self, PyObject *args) {
    int i=INDEF;
    if (! PyArg_ParseTuple(args, "|i", &i))
        return Polygon_Raise(ERR_ARG);
    if (i!=INDEF) {
        if ((i >= 0) && (i < self->gpc_p->num_contours))
            return Py_BuildValue("i", poly_c_orientation(self->gpc_p->contour+i));
        else
            return Polygon_Raise(ERR_IND);
    } else {
        PyObject *OL;
        OL = PyTuple_New(self->gpc_p->num_contours);
        for (i = 0; i < self->gpc_p->num_contours; i++)
            PyTuple_SetItem(OL, i, PyFloat_FromDouble(poly_c_orientation(self->gpc_p->contour+i)));
        return OL;
    }
}


#define DOCSTR_POLY_CENTER \
"center(|i)\n\
Calculates the center of gravity of one contour (when called with index i) or \n\
of the whole Polygon. The center may  be outside the contours or inside holes.\n\
This is not the center of the bounding box!\n\
:Arguments:\n\
    - optional i: integer\n\
:Returns:\n\
    a 2-tuple of float (x, y)\n"
static PyObject *Polygon_center(Polygon *self, PyObject *args) {
    int i=INDEF;
    double cx, cy;
    if (! PyArg_ParseTuple(args, "|i", &i))
        return Polygon_Raise(ERR_ARG);
    if (i!=INDEF) {
        if ((i >= 0) && (i < self->gpc_p->num_contours)) {
            if (poly_c_center(self->gpc_p->contour+i, &cx, &cy) !=0)
                return Polygon_Raise(ERR_INV);
        } else
            return Polygon_Raise(ERR_IND);
    } else {
        if (poly_p_center(self->gpc_p, &cx, &cy) != 0)
            return Polygon_Raise(ERR_INV);
    }
    return Py_BuildValue("dd", cx, cy);
}


#define DOCSTR_POLY_ASPECTRATIO \
"aspectRatio(|i)\n\
Returns the aspect ratio (ymax-ymin) / (xmax-xmin) of the bounding box of one \n\
contour (when called with index) or of the whole polygon.\n\
:Arguments:\n\
    - optional i: integer\n\
:Returns:\n\
    float\n"
static PyObject *Polygon_aspectRatio(Polygon *self, PyObject *args) {
    int i=INDEF;
    double x0, x1, y0, y1;
    if (! PyArg_ParseTuple(args, "|i", &i))
        return Polygon_Raise(ERR_ARG);
    if (i!=INDEF) {
        if ((i >= 0) && (i < self->gpc_p->num_contours))
            poly_c_boundingbox(self->gpc_p->contour+i, &x0, &x1, &y0, &y1);
        else
            return Polygon_Raise(ERR_IND);
    } else
        Polygon_getBoundingBox(self, &x0, &x1, &y0, &y1);
    return Py_BuildValue("d", ((x0 != x1) ? fabs((y1-y0)/(x1-x0)) : 0.0));
}


#define DOCSTR_POLY_BOUNDINGBOX \
"boundingBox(|i)\n\
Calculates the bounding box of one contour (when called with index i) or of the\n\
whole polygon. In the latter case the data is cached and used for following \n\
calls and internal calculations. The data will be recalculated automatically \n\
when this method is called after the polygon has changed.\n\
:Arguments:\n\
    - optional i: integer\n\
:Returns:\n\
    tuple of four floats: xmin, xmax, ymin and ymax\n"
static PyObject *Polygon_boundingBox(Polygon *self, PyObject *args) {
    int i=INDEF;
    double x0, x1, y0, y1;
    if (! PyArg_ParseTuple(args, "|i", &i))
        return Polygon_Raise(ERR_ARG);
    if (i!=INDEF) {
        if ((i >= 0) && (i < self->gpc_p->num_contours))
            poly_c_boundingbox(self->gpc_p->contour+i, &x0, &x1, &y0, &y1);
        else
            return Polygon_Raise(ERR_IND);
    } else
        Polygon_getBoundingBox(self, &x0, &x1, &y0, &y1);
    return Py_BuildValue("dddd", x0, x1,y0,y1);
}


#define DOCSTR_POLY_ISHOLE \
"isHole(|i)\n\
Returns the hole flag of a single contour (when called with index argument) or \n\
a list  of all flags when called without arguments.\n\
:Arguments:\n\
    - optional i: integer\n\
:Returns:\n\
    bool or list of bools\n"
static PyObject *Polygon_isHole(Polygon *self, PyObject *args) {
    int i=INDEF;
    if (! PyArg_ParseTuple(args, "|i", &i))
        return Polygon_Raise(ERR_ARG);
    if (i!=INDEF) {
        if ((i >= 0) && (i < self->gpc_p->num_contours))
            if (self->gpc_p->hole[i] > 0)
                Py_RETURN_TRUE;
            else 
                Py_RETURN_FALSE;
        else 
            return Polygon_Raise(ERR_IND);
    } else {
        PyObject *O;
        O = PyTuple_New(self->gpc_p->num_contours);
        for (i = 0; i < self->gpc_p->num_contours; i++)
            PyTuple_SetItem(O, i, PyBool_FromLong((self->gpc_p->hole[i] > 0) ? 1 : 0));
        return O;
    }
}


#define DOCSTR_POLY_ISSOLID \
"isSolid(|i)\n\
Returns the inverted hole flag of a single contour (when called with index \n\
argument) or a list  of all flags when called without arguments.\n\
:Arguments:\n\
    - optional i: integer\n\
:Returns:\n\
    bool or list of bools\n"
static PyObject *Polygon_isSolid(Polygon *self, PyObject *args) {
    int i=INDEF;
    if (! PyArg_ParseTuple(args, "|i", &i))
        return Polygon_Raise(ERR_ARG);
    if (i!=INDEF) {
        if ((i >= 0) && (i < self->gpc_p->num_contours))
            if (self->gpc_p->hole[i] > 0)
                Py_RETURN_FALSE;
            else 
                Py_RETURN_TRUE;
        else 
            return Polygon_Raise(ERR_IND);
    } else {
        PyObject *O;
        O = PyTuple_New(self->gpc_p->num_contours);
        for (i = 0; i < self->gpc_p->num_contours; i++)
            PyTuple_SetItem(O, i, PyBool_FromLong((self->gpc_p->hole[i] > 0) ? 0 : 1));
        return O;
    }
}


#define DOCSTR_POLY_ISINSIDE \
"isInside(x, y |, i)\n\
Point containment test: returns logical containment value for a single \n\
contour (when called with index) or of the whole Polygon. If point is exactly \n\
on the border, the value may be True or False, sorry!\n\
:Arguments:\n\
    - x: float\n\
    - y: float\n\
    - optional i: integer\n\
:Returns:\n\
    bool\n"
static PyObject *Polygon_isInside(Polygon *self, PyObject *args) {
    int i=INDEF, r=0;
    double x, y;
    if (! PyArg_ParseTuple(args, "dd|i", &x, &y, &i))
        return Polygon_Raise(ERR_ARG);
    if (i!=INDEF) {
        if ((i >= 0) && (i < self->gpc_p->num_contours)) {
            if ((r = poly_c_point_inside(self->gpc_p->contour+i, x, y)) == -1)
                return Polygon_Raise(ERR_INV);
        } else
            return Polygon_Raise(ERR_IND);
    } else {
        if ((r = poly_p_point_inside(self->gpc_p, x, y)) == -1)
            return Polygon_Raise(ERR_INV);
    }
    return Py_BuildValue("O", PyBool_FromLong(r));
}


#define DOCSTR_POLY_COVERS \
"covers(p)\n\
Tests if the polygon completely covers the other polygon p. At first the \n\
bounding boxes are tested for obvious cases and then an optional clipping is \n\
performed.\n\
:Arguments:\n\
    - p: Polygon\n\
:Returns:\n\
    bool\n"
static PyObject *Polygon_covers(Polygon *self, Polygon *other) {
    double x0, x1, y0, y1, X0, X1, Y0, Y1;
    gpc_polygon * pres;
    int r;
    if (! Polygon_Check(other))
        return Polygon_Raise(ERR_ARG);
    Polygon_getBoundingBox(self,  &x0, &x1, &y0, &y1);
    Polygon_getBoundingBox(other, &X0, &X1, &Y0, &Y1);
    /* first test if bounding box covers other boundingbox */ 
    if ((X0 < x0) || (X1 > x1) || (Y0 < y0) || (Y1 > y1))
        Py_RETURN_FALSE;
    /* still there? Let's do the full test... */  
    if (! (pres = poly_p_new())) return Polygon_Raise(ERR_MEM);
    gpc_polygon_clip(GPC_DIFF, other->gpc_p, self->gpc_p, pres);
    r = pres->num_contours;
    gpc_free_polygon(pres);
    free(pres);
    if (r > 0)
        Py_RETURN_FALSE;
    else
        Py_RETURN_TRUE;
}


#define DOCSTR_POLY_OVERLAPS \
"overlaps(q)\n\
Tests if the polygon overlaps the other polygon q. At first the bounding boxes \n\
are tested for obvious cases and then an optional clipping is performed.\n\
:Arguments:\n\
    - p: Polygon\n\
:Returns:\n\
    bool\n"
static PyObject *Polygon_overlaps(Polygon *self, Polygon *other) {
    double x0, x1, y0, y1, X0, X1, Y0, Y1;
    gpc_polygon * pres;
    int r;
    if (! Polygon_Check(other))
        return Polygon_Raise(ERR_ARG);
    Polygon_getBoundingBox(self,  &x0, &x1, &y0, &y1);
    Polygon_getBoundingBox(other, &X0, &X1, &Y0, &Y1);
    /* first test if bounding box overlaps other boundingbox */ 
    if ((X0 > x1) || (x0 > X1) || (Y0 > y1) || (y0 > Y1))
        Py_RETURN_FALSE;
    /* still there? Let's do the full test... */
    if (! (pres = poly_p_new())) return Polygon_Raise(ERR_MEM);
    gpc_polygon_clip(GPC_INT, other->gpc_p, self->gpc_p, pres);
    r = pres->num_contours;
    gpc_free_polygon(pres);
    free(pres);
    if (r > 0)
        Py_RETURN_TRUE;
    else
        Py_RETURN_FALSE;
}


#define DOCSTR_POLY_TRISTRIP \
"triStrip()\n\
Returns a list of tristrips describing the Polygon area. A tristrip is a list of \n\
triangles. The sum of all triangles fill the tristrip area. The triangles \n\
are usually not in a good shape for FEM methods!\n\
Each strip stores triangle data by an memory-efficient method. A strip is a \n\
tuple containing points (2-tuples). The first three items of the tuple belong \n\
to the first triangle. The second, third and fourth item are the corners of \n\
the second triangle. Item number three, four and five are the corners of the \n\
third triangle, (...you may guess the rest!). The number of triangles in a \n\
strip is the number of points minus 2.\n\
:Arguments:\n\
    None \n\
:Returns:\n\
    list of tuples of 2-tuples\n"
static PyObject *Polygon_triStrip(Polygon *self) {
    gpc_tristrip *t = (gpc_tristrip *)alloca(sizeof(gpc_tristrip));
    gpc_vertex_list *vl;
    PyObject *R, *TS;
    int i, j;
    t->num_strips = 0;
    t->strip = NULL;
    gpc_polygon_to_tristrip(self->gpc_p, t);
    switch (dataStyle) {
        case STYLE_TUPLE: {
            PyObject *P;
            gpc_vertex *v;
            R = PyTuple_New(t->num_strips);
            for (i=0; i < t->num_strips; i++) {
                vl = t->strip + i;
                v = vl->vertex;
                TS = PyTuple_New(vl->num_vertices);
                for (j=0; j < vl->num_vertices; j++) {
                    P = PyTuple_New(2);
                    PyTuple_SetItem(P, 0, PyFloat_FromDouble(v->x));
                    PyTuple_SetItem(P, 1, PyFloat_FromDouble(v->y));
                    PyTuple_SetItem(TS, j, P);
                    v++;
                }
                PyTuple_SetItem(R, i, TS);
            }
        } break;
        case STYLE_LIST: {
            PyObject *P;
            gpc_vertex *v;
            R = PyList_New(t->num_strips);
            for (i=0; i < t->num_strips; i++) {
                vl = t->strip + i;
                v = vl->vertex;
                TS = PyList_New(vl->num_vertices);
                for (j=0; j < vl->num_vertices; j++) {
                    P = PyTuple_New(2);
                    PyTuple_SetItem(P, 0, PyFloat_FromDouble(v->x));
                    PyTuple_SetItem(P, 1, PyFloat_FromDouble(v->y));
                    PyList_SetItem(TS, j, P);
                    v++;
                }
                PyList_SetItem(R, i, TS);
            }
        } break;
#ifdef WITH_NUMPY
        case STYLE_NUMPY: {
            npy_intp dims[2] = {0, 2};
            R = PyTuple_New(t->num_strips);
            for (i=0; i < t->num_strips; i++) {
                vl = t->strip + i;
                dims[0] = vl->num_vertices;
                TS = PyArray_SimpleNew(2, dims, NPY_DOUBLE);
                memcpy(PyArray_DATA((PyArrayObject *)TS), vl->vertex, sizeof(gpc_vertex)*vl->num_vertices);
                PyTuple_SetItem(R, i, (PyObject *)TS);
            }
        } break;
#endif /* WITH_NUMPY */
        default:
            return Polygon_Raise(PolyError, "Unknown data style");
    }
    gpc_free_tristrip(t);
    return R;
}


/* sample() was contributed by Thouis (Ray) Jones */
#define DOCSTR_POLY_SAMPLE \
"sample(rng)\n\
Returns a random sample somewhere within the polygon.\n\
:Arguments:\n\
   - rng : Random number generator, a function or method taking 0 arguments,\n\
           that returns a float [0.0..1.0] (e.g. python's random.random).\n\
:Returns:\n\
    - random point in the polygon as a 2-tuple\n"
static PyObject *Polygon_sample(Polygon *self, PyObject *args) {
    PyObject *rng, *val1, *val2, *val3, *res;
    double A;
    if (! PyArg_ParseTuple(args, "O", &rng))
        return Polygon_Raise(ERR_ARG);
    if (!PyCallable_Check(rng))
        return Polygon_Raise(ERR_ARG);

    res = NULL;

    Py_INCREF(rng);
    // Sampling requires three random values
    val1 = val2 = val3 = NULL;
    val1 = PyObject_CallObject(rng, NULL);
    val2 = PyObject_CallObject(rng, NULL);
    val3 = PyObject_CallObject(rng, NULL);
    Py_DECREF(rng);

    if (PyErr_Occurred()) {
        PyErr_PrintEx(1);
        Polygon_Raise(PolyError, "rng raised an error");
        goto cleanup;
    }

    if ((! PyFloat_Check(val1)) || (! PyFloat_Check(val2)) || (! PyFloat_Check(val3))) {
         Polygon_Raise(PolyError,
                       "rng returned something other than a float");
         goto cleanup;
    }
            
    A = poly_p_area(self->gpc_p);
    if (A == 0.0) {
      Polygon_Raise(PolyError,
                    "cannot sample from a zero-area polygon");
      goto cleanup;
    } else {
        gpc_tristrip *t = (gpc_tristrip *)alloca(sizeof(gpc_tristrip));
        gpc_vertex_list *vl;
        gpc_vertex_list one_tri;
        int i, j;
        gpc_vertex *tri_verts;
        double a, b, c, px, py;
      
        t->num_strips = 0;
        t->strip = NULL;
        gpc_polygon_to_tristrip(self->gpc_p, t);
        
        A *= PyFloat_AS_DOUBLE(val1);
        
        one_tri.num_vertices = 3;
        for (i=0; i < t->num_strips; i++) {
            vl = t->strip + i;
            for (j=0; j < vl->num_vertices - 2; j++) {
                one_tri.vertex = vl->vertex + j;
                A -= poly_c_area(& one_tri);
                if (A <= 0.0)
                    goto tri_found;
          }
        }
      tri_found:
        // sample a point from this triangle
        a = PyFloat_AS_DOUBLE(val2);
        b = PyFloat_AS_DOUBLE(val3);
        if ((a + b) > 1.0) {
            a = 1 - a;
            b = 1 - b;
        }
        c = 1 - a - b;
        tri_verts = one_tri.vertex;
        px = a * tri_verts[0].x + b * tri_verts[1].x + c * tri_verts[2].x;
        py = a * tri_verts[0].y + b * tri_verts[1].y + c * tri_verts[2].y;
        res = PyTuple_New(2);
        PyTuple_SetItem(res, 0, PyFloat_FromDouble(px));
        PyTuple_SetItem(res, 1, PyFloat_FromDouble(py));
        gpc_free_tristrip(t);
    }

  cleanup:
    Py_XDECREF(val1);
    Py_XDECREF(val2);
    Py_XDECREF(val3);
                
    return res;
}


static int Polygon_nonzero(PyObject *self) {
    return ((((Polygon *)self)->gpc_p->num_contours > 0) ? 1 : 0);
}


static PyObject *Polygon_opDiff(Polygon *self, Polygon *other) {
    gpc_polygon *ret;
    if (! Polygon_Check(other)) return Polygon_Raise(ERR_TYP);
    if (! (ret = poly_p_new())) return Polygon_Raise(ERR_MEM);
    gpc_polygon_clip(GPC_DIFF, self->gpc_p, other->gpc_p, ret);
    return (PyObject *)Polygon_NEW(ret);
}


static PyObject *Polygon_opUnion(Polygon *self, Polygon *other) {
    gpc_polygon *ret;
    if (! Polygon_Check(other)) return Polygon_Raise(ERR_TYP);
    if (! (ret = poly_p_new())) return Polygon_Raise(ERR_MEM);
    gpc_polygon_clip(GPC_UNION, self->gpc_p, other->gpc_p, ret);
    return (PyObject *)Polygon_NEW(ret);
}


static PyObject *Polygon_opXor(Polygon *self, Polygon *other) {
    gpc_polygon *ret;
    if (! Polygon_Check(other)) return Polygon_Raise(ERR_TYP);
    if (! (ret = poly_p_new())) return Polygon_Raise(ERR_MEM);
    gpc_polygon_clip(GPC_XOR, self->gpc_p, other->gpc_p, ret);
    return (PyObject *)Polygon_NEW(ret);
}


static PyObject *Polygon_opInt(Polygon *self, Polygon *other) {
    gpc_polygon *ret;
    if (! Polygon_Check(other)) return Polygon_Raise(ERR_TYP);
    if (! (ret = poly_p_new())) return Polygon_Raise(ERR_MEM);
    gpc_polygon_clip(GPC_INT, self->gpc_p, other->gpc_p, ret);
    return (PyObject *)Polygon_NEW(ret);
}


static PyNumberMethods Polygon_NumberMethods = {
    (PyCFunction)Polygon_opUnion, /* binaryfunc nb_add;        __add__ */
    (PyCFunction)Polygon_opDiff,  /* binaryfunc nb_subtract;   __sub__ */
    0,               /* binaryfunc nb_multiply;   __mul__ */
    0,               /* binaryfunc nb_remainder;  __mod__ */
    0,               /* binaryfunc nb_divmod;     __divmod__ */
    0,               /* ternaryfunc nb_power;     __pow__ */
    0,               /* unaryfunc nb_negative;    __neg__ */
    0,               /* unaryfunc nb_positive;    __pos__ */
    0,               /* unaryfunc nb_absolute;    __abs__ */
    Polygon_nonzero, /* inquiry nb_nonzero;       __nonzero__ */
    0,               /* unaryfunc nb_invert;      __invert__ */
    0,               /* binaryfunc nb_lshift;     __lshift__ */
    0,               /* binaryfunc nb_rshift;     __rshift__ */
    (PyCFunction)Polygon_opInt,   /* binaryfunc nb_and;        __and__ */
    (PyCFunction)Polygon_opXor,   /* binaryfunc nb_xor;        __xor__ */
    (PyCFunction)Polygon_opUnion, /* binaryfunc nb_or;         __or__ */
    0,               /* coercion nb_coerce;       __coerce__ */
    0,               /* unaryfunc nb_int;         __int__ */
    0,               /* unaryfunc nb_long;        __long__ */
    0,               /* unaryfunc nb_float;       __float__ */
    0,               /* unaryfunc nb_oct;         __oct__ */
    0,               /* unaryfunc nb_hex;         __hex__ */
};


static PySequenceMethods Polygon_SequenceMethods = {
    Polygon_len,     /* inquiry sq_length;             __len__ */
    0,               /* binaryfunc sq_concat;          __add__ */
    0,               /* intargfunc sq_repeat;          __mul__ */
    Polygon_getitem, /* intargfunc sq_item;            __getitem__ */
    0,               /* intintargfunc sq_slice;        __getslice__ */
    0,               /* intobjargproc sq_ass_item;     __setitem__ */
    0                /* intintobjargproc sq_ass_slice; __setslice__ */
};


static PyMethodDef Polygon_Methods[] = {
    {"addContour",    (PyCFunction)Polygon_addContour,  METH_VARARGS, DOCSTR_POLY_ADDCONTOUR},
    {"contour",       (PyCFunction)Polygon_getContour,  METH_VARARGS, DOCSTR_POLY_CONTOUR},
    {"read",          (PyCFunction)Polygon_read,        METH_VARARGS, DOCSTR_POLY_READ},
    {"write",         (PyCFunction)Polygon_write,       METH_VARARGS, DOCSTR_POLY_WRITE},
    {"simplify",      (PyCFunction)Polygon_simplify,    METH_VARARGS, DOCSTR_POLY_SIMPLIFY},
    {"shift",         (PyCFunction)Polygon_shift,       METH_VARARGS, DOCSTR_POLY_SHIFT},
    {"scale",         (PyCFunction)Polygon_scale,       METH_VARARGS, DOCSTR_POLY_SCALE},
    {"rotate",        (PyCFunction)Polygon_rotate,      METH_VARARGS, DOCSTR_POLY_ROTATE},
    {"warpToBox",     (PyCFunction)Polygon_warpToBox,   METH_VARARGS, DOCSTR_POLY_WARPTOBOX},
    {"flip",          (PyCFunction)Polygon_flip,        METH_VARARGS, DOCSTR_POLY_FLIP},
    {"flop",          (PyCFunction)Polygon_flop,        METH_VARARGS, DOCSTR_POLY_FLOP},
    {"area",          (PyCFunction)Polygon_area,        METH_VARARGS, DOCSTR_POLY_AREA},
    {"orientation",   (PyCFunction)Polygon_orientation, METH_VARARGS, DOCSTR_POLY_ORIENTATION},
    {"center",        (PyCFunction)Polygon_center,      METH_VARARGS, DOCSTR_POLY_CENTER},
    {"boundingBox",   (PyCFunction)Polygon_boundingBox, METH_VARARGS, DOCSTR_POLY_BOUNDINGBOX},
    {"aspectRatio",   (PyCFunction)Polygon_aspectRatio, METH_VARARGS, DOCSTR_POLY_ASPECTRATIO},
    {"isHole",        (PyCFunction)Polygon_isHole,      METH_VARARGS, DOCSTR_POLY_ISHOLE},
    {"isSolid",       (PyCFunction)Polygon_isSolid,     METH_VARARGS, DOCSTR_POLY_ISSOLID},
    {"isInside",      (PyCFunction)Polygon_isInside,    METH_VARARGS, DOCSTR_POLY_ISINSIDE},
    {"covers",        (PyCFunction)Polygon_covers,      METH_O,       DOCSTR_POLY_COVERS},
    {"overlaps",      (PyCFunction)Polygon_overlaps,    METH_O,       DOCSTR_POLY_OVERLAPS},
    {"nPoints",       (PyCFunction)Polygon_nPoints,     METH_VARARGS, DOCSTR_POLY_NPOINTS},
    {"triStrip",      (PyCFunction)Polygon_triStrip,    METH_NOARGS,  DOCSTR_POLY_TRISTRIP},
    {"cloneContour",  (PyCFunction)Polygon_cloneContour,METH_VARARGS, DOCSTR_POLY_CLONE_CONTOUR},
    {"sample",        (PyCFunction)Polygon_sample,      METH_VARARGS, DOCSTR_POLY_SAMPLE},
    {NULL, NULL}, /* sentinel */
};


static PyTypeObject Polygon_Type = {
    PyVarObject_HEAD_INIT(NULL, 0)
    "cPolygon.Polygon",             /*tp_name*/
    sizeof(Polygon),                 /*tp_basicsize*/
    0,                               /*tp_itemsize*/
    /* methods */
    (destructor)Polygon_dealloc,     /*tp_dealloc*/
    0,                               /*tp_print*/
    0,                               /*tp_getattr*/
    0,                               /*tp_setattr*/
    0,                               /*tp_compare*/
    Polygon_repr,                    /* (reprfunc)tp_repr*/
    &Polygon_NumberMethods,          /*tp_as_number*/
    &Polygon_SequenceMethods,        /*tp_as_sequence*/
    0,                               /*tp_as_mapping*/
    0,                               /*tp_hash*/
    0,                               /*tp_call*/
    0,                               /*tp_str*/
    0,                               /*tp_getattro*/
    0,                               /*tp_setattro*/
    0,                               /*tp_as_buffer*/
    Py_TPFLAGS_DEFAULT | Py_TPFLAGS_BASETYPE, /*tp_flags*/
    DOCSTR_POLY_TYPE,                /*tp_doc*/
    0,                               /*tp_traverse*/
    0,                               /*tp_clear*/
    0,                               /*tp_richcompare*/
    0,                               /*tp_weaklistoffset*/
    0,                               /*tp_iter*/
    0,                               /*tp_iternext*/
    Polygon_Methods,                 /*tp_methods*/
    0,                               /*tp_members*/
    0,                               /*tp_getset*/
    0,                               /*tp_base*/
    0,                               /*tp_dict*/
    0,                               /*tp_descr_get*/
    0,                               /*tp_descr_set*/
    0,                               /*tp_dictoffset*/
    (initproc)Polygon_init,          /*tp_init*/
    0,                               /*tp_alloc*/
    Polygon_new,                     /*tp_new*/
};


/***************************** Module *******************************/
#define DOCSTR_POLY_DATASTYLE \
"setDataStyle(s)\n\
The data style defines the type of objects returned by some functions.\n\
Point lists may be returned as tuples, lists or numpy.arrays (of compiled\n\
with support for numpy).\n\
:Arguments:\n\
    - s: one of STYLE_TUPLE, STYLE_LIST or STYLE_NUMPY\n\
:Returns:\n\
    None\n"
static PyObject * setDataStyle(PyObject *self, PyObject *arg) {
    if (! PyLong_Check(arg))
        return Polygon_Raise(ERR_ARG);
    dataStyle = PyLong_AsLong(arg);
    Py_RETURN_NONE;
}

#define DOCSTR_POLY_SETTOLERANCE \
"setTolerance(t)\n\
The tolerance is used to test if points are coincident. Increasing\n\
the value will be needed when point coordinates are not accurate\n\
(maybe because of a lot of conversions). Defaults to DBL_EPSILON.\n\
:Arguments:\n\
    - t: float\n\
:Returns:\n\
    None\n"
static PyObject * setEpsilon(PyObject *self, PyObject *arg) {
    if (PyFloat_Check(arg))
        GPC_EPSILON = PyFloat_AsDouble(arg);
    else if (PyLong_Check(arg))
        GPC_EPSILON = PyLong_AsLong(arg);
    else
        return Polygon_Raise(ERR_ARG);
    Py_RETURN_NONE;
}

#define DOCSTR_POLY_GETTOLERANCE \
"getTolerance()\n\
Returns the tolerance value. See setTolerance() for details.\n\
:Arguments:\n\
    None\n\
:Returns:\n\
    float\n"
static PyObject * getEpsilon(PyObject *self) {
    return Py_BuildValue("d", GPC_EPSILON);
}

static PyMethodDef cPolygonMethods[] = {
    {"setDataStyle", (PyCFunction)setDataStyle, METH_O      , DOCSTR_POLY_DATASTYLE},
    {"setTolerance", (PyCFunction)setEpsilon,   METH_O      , DOCSTR_POLY_SETTOLERANCE},
    {"getTolerance", (PyCFunction)getEpsilon,   METH_NOARGS , DOCSTR_POLY_GETTOLERANCE},
    {NULL,      NULL}        /* Sentinel */
};

static struct PyModuleDef cPolygonModule = {
   PyModuleDef_HEAD_INIT,
   "cPolygon",
   DOCSTR_POLY_MODULE,
   -1,
   cPolygonMethods
};

PyMODINIT_FUNC PyInit_cPolygon(void) {
    PyObject *m;
    if (PyType_Ready(&Polygon_Type) < 0) return NULL;
    m = PyModule_Create(&cPolygonModule);
    if (m == NULL)
        return NULL;
#ifdef WITH_NUMPY
    import_array();
#endif /* WITH_NUMPY */
    PolyError = PyErr_NewException("cPolygon.Error", NULL, NULL);
    Py_INCREF(PolyError);
    PyModule_AddObject(m, "Error", PolyError);
    Py_INCREF(&Polygon_Type);
    PyModule_AddObject(m, "Polygon", (PyObject *)(&Polygon_Type));
    PyModule_AddObject(m, "STYLE_TUPLE", (PyObject *)PyLong_FromLong(STYLE_TUPLE));
    PyModule_AddObject(m, "STYLE_LIST", (PyObject *)PyLong_FromLong(STYLE_LIST));
#ifdef WITH_NUMPY
    PyModule_AddObject(m, "STYLE_NUMPY", (PyObject *)PyLong_FromLong(STYLE_NUMPY));
    PyModule_AddObject(m, "withNumPy", (PyObject *)Py_True);
#else
    PyModule_AddObject(m, "withNumPy", (PyObject *)Py_False);
#endif /* WITH_NUMPY */
    PyModule_AddObject(m, "version", (PyObject *)PyUnicode_FromString(POLY_VERSION));
    PyModule_AddObject(m, "author", (PyObject *)PyUnicode_FromString(POLY_AUTHOR));
    PyModule_AddObject(m, "license", (PyObject *)PyUnicode_FromString(POLY_LICENSE));
    return m;
}
