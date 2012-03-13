/******************************************************************************
* Copyright (c) 2011, Michael P. Gerlek (mpg@flaxen.com)
*
* All rights reserved.
*
* Redistribution and use in source and binary forms, with or without
* modification, are permitted provided that the following
* conditions are met:
*
*     * Redistributions of source code must retain the above copyright
*       notice, this list of conditions and the following disclaimer.
*     * Redistributions in binary form must reproduce the above copyright
*       notice, this list of conditions and the following disclaimer in
*       the documentation and/or other materials provided
*       with the distribution.
*     * Neither the name of Hobu, Inc. or Flaxen Geo Consulting nor the
*       names of its contributors may be used to endorse or promote
*       products derived from this software without specific prior
*       written permission.
*
* THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
* "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
* LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
* FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE
* COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
* INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
* BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS
* OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED
* AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
* OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT
* OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY
* OF SUCH DAMAGE.
****************************************************************************/

#include <pdal/pdal_internal.hpp>
#ifdef PDAL_HAVE_PYTHON

#include <pdal/plang/PythonSupport.hpp>

#include <string>
#include <iostream>

#ifdef PDAL_COMPILER_MSVC
#  pragma warning(disable: 4127)  // conditional expression is constant
#endif

#include <Python.h>
#include <numpy/arrayobject.h>


namespace pdal { namespace plang {


PythonEnvironment::PythonEnvironment()
    : m_mod1(NULL)
    , m_dict1(NULL)
    , m_fexcp(NULL)
{
    return;
}


PythonEnvironment::~PythonEnvironment()
{
    return;
}


bool PythonEnvironment::startup()
{
    // launch the python interpreter
    Py_Initialize();

    // this macro is defined be NumPy and must be included
    if (_import_array() < 0)
        die(20);

    // load module "traceback" (for error handling)
    m_mod1 = PyImport_ImportModule( "traceback" );
    if (!m_mod1) die(6);

    // get dictionary of available items in the modules
    m_dict1 = PyModule_GetDict(m_mod1);

    // grab the functions we are interested in
    m_fexcp = PyDict_GetItemString(m_dict1, "format_exception");
    if (!m_fexcp) die(5);

    if (!PyCallable_Check(m_fexcp)) die(4);

    return true;
}


bool PythonEnvironment::shutdown()
{
    Py_XDECREF(m_fexcp);

    Py_XDECREF(m_mod1);

    Py_Finalize();

    return true;
}


void PythonEnvironment::die(int i)
{
    printf("error: %d\n", i);
    abort();
}


void PythonEnvironment::output_result( PyObject* rslt )
{
    if( !rslt ) {
        printf("output_result: argument must be a valid pointer\n");
        return;
    }

    // output scalar result
    if( PyFloat_Check(rslt) ) {
        printf( "result: %f\n", PyFloat_AsDouble(rslt) );
        return;
    }

    if( !PyArray_Check(rslt) ) {
        die(10);
    }

    PyArrayObject* obj = PyArray_GETCONTIGUOUS( (PyArrayObject*) rslt );
    int ndims = obj->nd;
    npy_intp* dims = obj->dimensions; // not copying data
    double* data = (double*) obj->data; // not copying data
    int i, j, k = 0;

    if( ndims == 1 ) 
    {
        // output vector result
        for( i=0; i<dims[0]; i++ )
            printf( "element: %i value: %f\n", i, data[k++] );
        printf("\n");
    }
    else if( ndims == 2 ) 
    {
        // output matrix result
        for( i=0; i<dims[0]; i++ ) {
            for( j=0; j<dims[1]; j++ )
                printf( "%f ", data[k++] );
            printf( "\n" );
        }
    }
    else
    {
        // output N-D result
        for( i=0; i<ndims; i++ )
            for( j=0; j<dims[i]; j++ )
                printf( "dimension: %i element: %i value: %f\n", i, j, data[k++] );
    }

    // clean
    Py_XDECREF(obj);
}


void PythonEnvironment::handle_error(PyObject*)
{
    PyObject* fe = m_fexcp;

    // get exception info
    PyObject *type, *value, *traceback;
    PyErr_Fetch( &type, &value, &traceback );
    PyErr_NormalizeException( &type, &value, &traceback );

    // create a argument for "format exception"
    PyObject* args = PyTuple_New(3);
    PyTuple_SetItem( args, 0, type );
    PyTuple_SetItem( args, 1, value );
    PyTuple_SetItem( args, 2, traceback );

    // get a list of string describing what went wrong
    PyObject* output = PyObject_CallObject( fe, args );

    // print error message
    int i, n = PyList_Size( output );
    for( i=0; i<n; i++ ) printf( "%s", PyString_AsString( PyList_GetItem( output, i ) ) );

    // clean up
    Py_XDECREF( args );
    Py_XDECREF( output );
}


// ==========================================================================


PythonMethod::PythonMethod(PythonEnvironment& env, const std::string& source)
    : m_env(env)
    , m_source(source)
{
    return;
}


bool PythonMethod::compile()
{
    return true;
}


inline
static int getPythonDataType(const Dimension& dim)
{
    const int siz = dim.getByteSize();

    switch (dim.getInterpretation())
    {
    case dimension::SignedByte:
        switch (siz)
        {
        case 1:
            return PyArray_BYTE;
        }
        break;
    case dimension::UnsignedByte:
        switch (siz)
        {
        case 1:
            return PyArray_UBYTE;
        }
        break;
    case dimension::Float:
        switch (siz)
        {
        case 4:
            return PyArray_FLOAT;
        case 8:
            return PyArray_DOUBLE;
        }
        break;
    case dimension::SignedInteger:
        switch (siz)
        {
        case 4:
            return PyArray_INT;
        case 8:
            return PyArray_LONGLONG;
        }
        break;
    case dimension::UnsignedInteger:
        switch (siz)
        {
        case 4:
            return PyArray_UINT;
        case 8:
            return PyArray_ULONGLONG;
        }
        break;
    }

    assert(0);

    return -1;
}


bool PythonMethod::beginChunk(PointBuffer& buffer)
{
    const Schema& schema = buffer.getSchema();


    const int nelem = buffer.getNumPoints();
    npy_intp mydims = { nelem };
    int nd = 1;
    npy_intp* dims = &mydims;
    npy_intp stride = schema.getByteSize();
    npy_intp* strides = &stride;
    int flags = NPY_CARRAY; // NPY_BEHAVED

    m_pyInputArrays.clear();
    m_varsIn = PyDict_New();
    m_varsOut = PyDict_New();

    schema::Map const& map = schema.getDimensions();
    schema::index_by_index const& idx = map.get<schema::index>();
    for (schema::index_by_index::const_iterator iter = idx.begin(); iter != idx.end(); ++iter)
    {
        const Dimension& dim = *iter;
        const std::string& name = dim.getName();
        const int pyDataType = getPythonDataType(dim);
        
        boost::uint8_t* data = buffer.getData(0) + dim.getByteOffset();

        PyObject* pyArray = PyArray_New(&PyArray_Type, nd, dims, pyDataType, strides, data, 0, flags, NULL);
        m_pyInputArrays.push_back(pyArray);
        PyDict_SetItemString(m_varsIn, name.c_str(), pyArray);
    }

    return true;
}


bool PythonMethod::endChunk(PointBuffer& buffer)
{
    const Schema& schema = buffer.getSchema();

    schema::Map const& map = schema.getDimensions();
    schema::index_by_index const& idx = map.get<schema::index>();
    for (schema::index_by_index::const_iterator iter = idx.begin(); iter != idx.end(); ++iter)
    {
        const Dimension& dim = *iter;
        const std::string& name = dim.getName();
        
        PyObject* xarr = PyDict_GetItemString(m_varsOut, name.c_str());
        if (!xarr) continue;
        assert(PyArray_Check(xarr));
        PyArrayObject* arr = (PyArrayObject*)xarr;

        npy_intp one=0;
        const int pyDataType = getPythonDataType(dim);

        if (pyDataType == PyArray_DOUBLE)
        {
            double* data = (double*)PyArray_GetPtr(arr, &one);

            const unsigned int cnt = buffer.getNumPoints();
            for (unsigned int i=0; i<cnt; i++)
            {
                buffer.setField<double>(dim, i, data[i]);
            }
        }
        else if (pyDataType == PyArray_ULONGLONG)
        {
            boost::uint64_t* data = (boost::uint64_t*)PyArray_GetPtr(arr, &one);

            const unsigned int cnt = buffer.getNumPoints();
            for (unsigned int i=0; i<cnt; i++)
            {
                buffer.setField<boost::uint64_t>(dim, i, data[i]);
            }
        }
        else
            assert(0);
    }

    Py_XDECREF(m_scriptResult);

    Py_XDECREF(m_scriptArgs); // also decrements script and vars

    for (unsigned int i=0; i<m_pyInputArrays.size(); i++)
    {
        PyObject* obj = m_pyInputArrays[i];
        Py_XDECREF(obj);
    }
    m_pyInputArrays.clear();

    return true;
}


bool PythonMethod::execute()
{
    PyObject* compiled = Py_CompileString(m_source.c_str(), "YowModule", Py_file_input);
    if (!compiled) m_env.die(6);
    PyObject* module = PyImport_ExecCodeModule("YowModule", compiled);
    if (!module) m_env.die(6);

    PyObject* dict = PyModule_GetDict(module);

    PyObject* func = PyDict_GetItemString(dict, "yow");
    if (!func) m_env.die(5);
    if (!PyCallable_Check(func)) m_env.die(4);
  
    Py_INCREF(m_varsIn);
    Py_INCREF(m_varsOut);
    m_scriptArgs = PyTuple_New(2);
    PyTuple_SetItem(m_scriptArgs, 0, m_varsIn);
    PyTuple_SetItem(m_scriptArgs, 1, m_varsOut);

    m_scriptResult = PyObject_CallObject(func, m_scriptArgs);
    if (!m_scriptResult) m_env.handle_error(0);
    
    return true;
}




} } //namespaces

#endif
