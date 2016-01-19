#define NPY_NO_DEPRECATED_API NPY_1_9_API_VERSION

extern "C" {
    #include <Python.h>
    #include <numpy/arrayobject.h>
    #include "docstrings.h"
}
#include "mcopt.h"
#include <exception>
#include <cassert>
#include <stdio.h>
#include <string>

class WrongDimensions : public std::exception
{
public:
    WrongDimensions() {}
    const char* what() const noexcept { return msg.c_str(); }

private:
    std::string msg = "The dimensions were incorrect";
};

class BadArrayLayout : public std::exception
{
public:
    BadArrayLayout() {}
    const char* what() const noexcept { return msg.c_str(); }

private:
    std::string msg = "The matrix was not contiguous";
};

static std::vector<double> convertPyArrayToVector(PyArrayObject* pyarr)
{
    int ndim = PyArray_NDIM(pyarr);
    if (ndim != 1) throw WrongDimensions();
    npy_intp* dims = PyArray_SHAPE(pyarr);

    double* dataPtr = static_cast<double*>(PyArray_DATA(pyarr));
    return std::vector<double>(dataPtr, dataPtr+dims[0]);
}

static const std::vector<npy_intp> getPyArrayDimensions(PyArrayObject* pyarr)
{
    npy_intp ndims = PyArray_NDIM(pyarr);
    npy_intp* dims = PyArray_SHAPE(pyarr);
    std::vector<npy_intp> result;
    for (int i = 0; i < ndims; i++) {
        result.push_back(dims[i]);
    }
    return result;
}

/* Checks the dimensions of the given array. Pass -1 for either dimension to say you don't
 * care what the size is in that dimension. Pass dimensions (X, 1) for a vector.
 */
static bool checkPyArrayDimensions(PyArrayObject* pyarr, const npy_intp dim0, const npy_intp dim1)
{
    const auto dims = getPyArrayDimensions(pyarr);
    assert(dims.size() <= 2 and dims.size() > 0);
    if (dims.size() == 1) {
        return (dims[0] == dim0) and (dim1 == 1);
    }
    else {
        return (dims[0] == dim0 or dim0 == -1) and (dims[1] == dim1 or dim1 == -1);
    }
}

static arma::mat convertPyArrayToArma(PyArrayObject* pyarr, int nrows, int ncols)
{
    if (!checkPyArrayDimensions(pyarr, nrows, ncols)) throw WrongDimensions();
    const auto dims = getPyArrayDimensions(pyarr);
    if (dims.size() == 1) {
        double* dataPtr = static_cast<double*>(PyArray_DATA(pyarr));
        return arma::vec(dataPtr, dims[0], true);
    }
    else {
        // Convert the array to a Fortran-contiguous (col-major) array of doubles, as required by Armadillo
        PyArray_Descr* reqDescr = PyArray_DescrFromType(NPY_DOUBLE);
        if (reqDescr == NULL) throw std::bad_alloc();
        PyArrayObject* cleanArr = (PyArrayObject*)PyArray_FromArray(pyarr, reqDescr, NPY_ARRAY_FARRAY);
        if (cleanArr == NULL) throw std::bad_alloc();
        reqDescr = NULL;  // The new reference from DescrFromType was stolen by FromArray

        double* dataPtr = static_cast<double*>(PyArray_DATA(cleanArr));
        arma::mat result (dataPtr, dims[0], dims[1], true);  // this copies the data from cleanArr
        Py_DECREF(cleanArr);
        return result;
    }
}

static PyObject* convertArmaToPyArray(const arma::mat& matrix)
{
    npy_intp ndim = matrix.is_colvec() ? 1 : 2;
    npy_intp nRows = static_cast<npy_intp>(matrix.n_rows);  // NOTE: This narrows the integer
    npy_intp nCols = static_cast<npy_intp>(matrix.n_cols);
    npy_intp dims[2] = {nRows, nCols};

    PyObject* result = PyArray_SimpleNew(ndim, dims, NPY_DOUBLE);
    if (result == NULL) throw std::bad_alloc();

    double* resultDataPtr = static_cast<double*>(PyArray_DATA((PyArrayObject*)result));
    for (int i = 0; i < nRows; i++) {
        for (int j = 0; j < nCols; j++) {
            resultDataPtr[i * nCols + j] = matrix(i, j);
        }
    }

    return result;
}

// -------------------------------------------------------------------------------------------------------------------

extern "C" {
    typedef struct MCTracker {
        PyObject_HEAD
        mcopt::Tracker* tracker = NULL;
    } MCTracker;

    static int MCTracker_init(MCTracker* self, PyObject* args, PyObject* kwargs)
    {
        unsigned massNum;
        unsigned chargeNum;
        PyArrayObject* elossArray = NULL;
        double efield[3];
        double bfield[3];

        char* kwlist[] = {"mass_num", "charge_num", "eloss", "efield", "bfield", NULL};

        if (!PyArg_ParseTupleAndKeywords(args, kwargs, "IIO!(ddd)(ddd)", kwlist,
                                         &massNum, &chargeNum, &PyArray_Type, &elossArray,
                                         &efield[0], &efield[1], &efield[2],
                                         &bfield[0], &bfield[1], &bfield[2])) {
            return -1;
        }

        std::vector<double> eloss;
        try {
            eloss = convertPyArrayToVector(elossArray);
        }
        catch (std::exception& err) {
            PyErr_SetString(PyExc_ValueError, err.what());
            return -1;
        }

        if (self->tracker != NULL) {
            delete self->tracker;
            self->tracker = NULL;
        }

        self->tracker = new mcopt::Tracker(massNum, chargeNum, eloss, arma::vec(efield, 3), arma::vec(bfield, 3));
        return 0;
    }

    static void MCTracker_dealloc(MCTracker* self)
    {
        if (self->tracker != NULL) {
            delete self->tracker;
        }
    }

    static PyObject* MCTracker_trackParticle(MCTracker* self, PyObject* args)
    {
        double x0, y0, z0, enu0, azi0, pol0;

        if (self->tracker == NULL) {
            PyErr_SetString(PyExc_RuntimeError, "The internal mcopt::Tracker object was NULL.");
            return NULL;
        }
        if (!PyArg_ParseTuple(args, "dddddd", &x0, &y0, &z0, &enu0, &azi0, &pol0)) {
            return NULL;
        }

        mcopt::Track tr;
        try {
            tr = self->tracker->trackParticle(x0, y0, z0, enu0, azi0, pol0);
        }
        catch (const std::exception& e) {
            PyErr_SetString(PyExc_RuntimeError, e.what());
            return NULL;
        }

        PyObject* result = NULL;
        try {
            result = convertArmaToPyArray(tr.getMatrix());
        }
        catch (const std::bad_alloc&){
            PyErr_NoMemory();
            return NULL;
        }
        return result;
    }

    static PyMethodDef MCTracker_methods[] = {
        {"track_particle", (PyCFunction)MCTracker_trackParticle, METH_VARARGS,
         "Track a particle"
        },
        {NULL}  /* Sentinel */
    };

    static PyTypeObject MCTrackerType = {
        PyVarObject_HEAD_INIT(NULL, 0)
        "mcopt_wrapper.Tracker",   /* tp_name */
        sizeof(MCTracker),         /* tp_basicsize */
        0,                         /* tp_itemsize */
        (destructor)MCTracker_dealloc, /* tp_dealloc */
        0,                         /* tp_print */
        0,                         /* tp_getattr */
        0,                         /* tp_setattr */
        0,                         /* tp_reserved */
        0,                         /* tp_repr */
        0,                         /* tp_as_number */
        0,                         /* tp_as_sequence */
        0,                         /* tp_as_mapping */
        0,                         /* tp_hash  */
        0,                         /* tp_call */
        0,                         /* tp_str */
        0,                         /* tp_getattro */
        0,                         /* tp_setattro */
        0,                         /* tp_as_buffer */
        Py_TPFLAGS_DEFAULT | Py_TPFLAGS_BASETYPE,        /* tp_flags */
        "mcopt Tracker",           /* tp_doc */
        0,                         /* tp_traverse */
        0,                         /* tp_clear */
        0,                         /* tp_richcompare */
        0,                         /* tp_weaklistoffset */
        0,                         /* tp_iter */
        0,                         /* tp_iternext */
        MCTracker_methods,         /* tp_methods */
        0,                         /* tp_members */
        0,                         /* tp_getset */
        0,                         /* tp_base */
        0,                         /* tp_dict */
        0,                         /* tp_descr_get */
        0,                         /* tp_descr_set */
        0,                         /* tp_dictoffset */
        (initproc)MCTracker_init,  /* tp_init */
        0,                         /* tp_alloc */
        0,                         /* tp_new */
    };

    // ---------------------------------------------------------------------------------------------------------------

    typedef struct MCMCminimizer {
        PyObject_HEAD
        mcopt::MCminimizer* minimizer = NULL;
    } MCMCminimizer;

    static int MCMCminimizer_init(MCMCminimizer* self, PyObject* args, PyObject* kwargs)
    {
        unsigned massNum, chargeNum;
        PyArrayObject* elossArray = NULL;
        double efield[3], bfield[3];
        double ioniz;

        char* kwlist[] = {"mass_num", "charge_num", "eloss", "efield", "bfield", "ioniz", NULL};

        if (!PyArg_ParseTupleAndKeywords(args, kwargs, "IIO!(ddd)(ddd)d", kwlist,
                                         &massNum, &chargeNum, &PyArray_Type, &elossArray,
                                         &efield[0], &efield[1], &efield[2],
                                         &bfield[0], &bfield[1], &bfield[2], &ioniz)) {
            return -1;
        }

        std::vector<double> eloss;
        try {
            eloss = convertPyArrayToVector(elossArray);
        }
        catch (std::exception& err) {
            PyErr_SetString(PyExc_ValueError, err.what());
            return -1;
        }

        if (self->minimizer != NULL) {
            delete self->minimizer;
            self->minimizer = NULL;
        }

        self->minimizer = new mcopt::MCminimizer(mcopt::Tracker(massNum, chargeNum, eloss,
                                                                arma::vec(efield, 3), arma::vec(bfield, 3)));
        return 0;
    }

    static void MCMCminimizer_dealloc(MCMCminimizer* self)
    {
        if (self->minimizer != NULL) {
            delete self->minimizer;
            self->minimizer = NULL;
        }
    }

    static PyObject* MCMCminimizer_minimize(MCMCminimizer* self, PyObject* args, PyObject* kwargs)
    {
        PyArrayObject* ctr0Arr = NULL;
        PyArrayObject* sig0Arr = NULL;
        PyArrayObject* trueValuesArr = NULL;
        unsigned numIters = 10;
        unsigned numPts = 200;
        double redFactor = 0.8;
        bool details = false;

        char* kwlist[] = {"ctr0", "sig0", "true_values", "num_iters", "num_pts", "red_factor", "details", NULL};

        if (self->minimizer == NULL) {
            PyErr_SetString(PyExc_RuntimeError, "The internal mcopt::MCminimizer object was NULL.");
            return NULL;
        }
        if (!PyArg_ParseTupleAndKeywords(args, kwargs, "O!O!O!|IIdp", kwlist,
                                         &PyArray_Type, &ctr0Arr, &PyArray_Type, &sig0Arr,
                                         &PyArray_Type, &trueValuesArr, &numIters, &numPts,
                                         &redFactor, &details)) {
            return NULL;
        }

        arma::vec ctr0, sig0;
        arma::mat trueValues;
        try {
            ctr0 = convertPyArrayToArma(ctr0Arr, 7, 1);
            sig0 = convertPyArrayToArma(sig0Arr, 7, 1);
            trueValues = convertPyArrayToArma(trueValuesArr, -1, 4);
        }
        catch (std::exception& err) {
            PyErr_SetString(PyExc_ValueError, err.what());
            return NULL;
        }

        arma::vec ctr;
        arma::mat allParams;
        arma::vec minChis;
        arma::vec goodParamIdx;
        try {
            std::tie(ctr, allParams, minChis, goodParamIdx) =
                self->minimizer->minimize(ctr0, sig0, trueValues, numIters, numPts, redFactor);
        }
        catch (std::exception& err) {
            PyErr_SetString(PyExc_RuntimeError, err.what());
            return NULL;
        }

        PyObject* ctrArr = NULL;
        try {
            ctrArr = convertArmaToPyArray(ctr);
        }
        catch (const std::bad_alloc&) {
            PyErr_NoMemory();
            return NULL;
        }

        if (details) {
            PyObject* allParamsArr = NULL;
            PyObject* minChisArr = NULL;
            PyObject* goodParamIdxArr = NULL;

            try {
                allParamsArr = convertArmaToPyArray(allParams);
                minChisArr = convertArmaToPyArray(minChis);
                goodParamIdxArr = convertArmaToPyArray(goodParamIdx);
            }
            catch (const std::bad_alloc&) {
                Py_DECREF(ctrArr);
                Py_XDECREF(allParamsArr);
                Py_XDECREF(minChisArr);
                Py_XDECREF(goodParamIdxArr);

                PyErr_NoMemory();
                return NULL;
            }

            PyObject* result = Py_BuildValue("OOOO", ctrArr, minChisArr, allParamsArr, goodParamIdxArr);
            Py_DECREF(ctrArr);
            Py_DECREF(allParamsArr);
            Py_DECREF(minChisArr);
            Py_DECREF(goodParamIdxArr);
            return result;
        }
        else {
            double lastChi = minChis(minChis.n_rows-1);
            PyObject* result = Py_BuildValue("Od", ctrArr, lastChi);
            Py_DECREF(ctrArr);
            return result;
        }
    }

    static PyMethodDef MCMCminimizer_methods[] = {
        {"minimize", (PyCFunction)MCMCminimizer_minimize, METH_VARARGS | METH_KEYWORDS,
         "Perform MC minimization"
        },
        {NULL}  /* Sentinel */
    };

    static PyTypeObject MCMCminimizerType = {
        PyVarObject_HEAD_INIT(NULL, 0)
        "mcopt_wrapper.Minimizer", /* tp_name */
        sizeof(MCMCminimizer),     /* tp_basicsize */
        0,                         /* tp_itemsize */
        (destructor)MCMCminimizer_dealloc, /* tp_dealloc */
        0,                         /* tp_print */
        0,                         /* tp_getattr */
        0,                         /* tp_setattr */
        0,                         /* tp_reserved */
        0,                         /* tp_repr */
        0,                         /* tp_as_number */
        0,                         /* tp_as_sequence */
        0,                         /* tp_as_mapping */
        0,                         /* tp_hash  */
        0,                         /* tp_call */
        0,                         /* tp_str */
        0,                         /* tp_getattro */
        0,                         /* tp_setattro */
        0,                         /* tp_as_buffer */
        Py_TPFLAGS_DEFAULT | Py_TPFLAGS_BASETYPE,    /* tp_flags */
        "mcopt Minimizer",         /* tp_doc */
        0,                         /* tp_traverse */
        0,                         /* tp_clear */
        0,                         /* tp_richcompare */
        0,                         /* tp_weaklistoffset */
        0,                         /* tp_iter */
        0,                         /* tp_iternext */
        MCMCminimizer_methods,     /* tp_methods */
        0,                         /* tp_members */
        0,                         /* tp_getset */
        0,                         /* tp_base */
        0,                         /* tp_dict */
        0,                         /* tp_descr_get */
        0,                         /* tp_descr_set */
        0,                         /* tp_dictoffset */
        (initproc)MCMCminimizer_init,  /* tp_init */
        0,                         /* tp_alloc */
        0,                         /* tp_new */
    };

    //  --------------------------------------------------------------------------------------------------------------

    static PyObject* mcopt_wrapper_find_deviations(PyObject* self, PyObject* args)
    {
        PyArrayObject* simArr = NULL;
        PyArrayObject* expArr = NULL;

        if (!PyArg_ParseTuple(args, "O!O!", &PyArray_Type, &simArr, &PyArray_Type, &expArr)) {
            return NULL;
        }

        PyObject* devArr = NULL;
        try {
            arma::mat simMat = convertPyArrayToArma(simArr, -1, -1);
            arma::mat expMat = convertPyArrayToArma(expArr, -1, -1);

            // printf("SimMat has shape (%lld, %lld)", simMat.n_rows, simMat.n_cols);
            // printf("ExpMat has shape (%lld, %lld)", expMat.n_rows, expMat.n_cols);

            arma::mat devs = mcopt::MCminimizer::findDeviations(simMat, expMat);

            devArr = convertArmaToPyArray(devs);
        }
        catch (std::bad_alloc) {
            PyErr_NoMemory();
            Py_XDECREF(devArr);
            return NULL;
        }
        catch (std::exception& err) {
            PyErr_SetString(PyExc_RuntimeError, err.what());
            Py_XDECREF(devArr);
            return NULL;
        }

        return devArr;
    }

    static PyMethodDef mcopt_wrapper_methods[] =
    {
        {"find_deviations", mcopt_wrapper_find_deviations, METH_VARARGS, mcopt_wrapper_find_deviations_docstring},
        {NULL, NULL, 0, NULL}
    };

    static struct PyModuleDef mcopt_wrapper_module = {
       PyModuleDef_HEAD_INIT,
       "mcopt_wrapper",   /* name of module */
       NULL, /* module documentation, may be NULL */
       -1,       /* size of per-interpreter state of the module,
                    or -1 if the module keeps state in global variables. */
       mcopt_wrapper_methods
    };

    PyMODINIT_FUNC
    PyInit_mcopt_wrapper(void)
    {
        import_array();

        MCTrackerType.tp_new = PyType_GenericNew;
        if (PyType_Ready(&MCTrackerType) < 0) return NULL;

        MCMCminimizerType.tp_new = PyType_GenericNew;
        if (PyType_Ready(&MCMCminimizerType) < 0) return NULL;

        PyObject* m = PyModule_Create(&mcopt_wrapper_module);
        if (m == NULL) return NULL;

        Py_INCREF(&MCTrackerType);
        PyModule_AddObject(m, "Tracker", (PyObject*)&MCTrackerType);

        Py_INCREF(&MCMCminimizerType);
        PyModule_AddObject(m, "Minimizer", (PyObject*)&MCMCminimizerType);

        return m;
    }
}
