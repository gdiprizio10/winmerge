/////////////////////////////////////////////////////////////////////////////
//    WinMerge:  an interactive diff/merge utility
//    Copyright (C) 1997-2000  Thingamahoochie Software
//    Author: Dean Grimm
//
//    This program is free software; you can redistribute it and/or modify
//    it under the terms of the GNU General Public License as published by
//    the Free Software Foundation; either version 2 of the License, or
//    (at your option) any later version.
//
//    This program is distributed in the hope that it will be useful,
//    but WITHOUT ANY WARRANTY; without even the implied warranty of
//    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
//    GNU General Public License for more details.
//
//    You should have received a copy of the GNU General Public License
//    along with this program; if not, write to the Free Software
//    Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
//
/////////////////////////////////////////////////////////////////////////////
/**
 * @file multiformatText.cpp
 *
 * @brief Implementation of class storageForPlugins
 *
 * @date  Created: 2003-11-24
 */ 
// ID line follows -- this is updated by SVN
// $Id: multiformatText.cpp 7082 2010-01-03 22:15:50Z sdottaka $

#define NOMINMAX
#include "multiformatText.h"
#include <vector>
#include <algorithm>
#include <cstring>
#include <cassert>
#include <boost/scoped_array.hpp>
#include <boost/scoped_ptr.hpp>
#include <Poco/SharedMemory.h>
#include <Poco/FileStream.h>
#include <Poco/ByteOrder.h>
#include <Poco/Buffer.h>
#include <Poco/Exception.h>
#include "unicoder.h"
#include "ExConverter.h"
#include "paths.h"
#include "UniFile.h"
#include "codepage.h"
#include "Environment.h"
#include "TFile.h"
#include "MergeApp.h"

using Poco::SharedMemory;
using Poco::FileOutputStream;
using Poco::ByteOrder;
using Poco::Exception;
using Poco::Buffer;
using Poco::Int64;

////////////////////////////////////////////////////////////////////////////////

static void *GetVariantArrayData(VARIANT& array, unsigned& size)
{
	char * parrayData;
	SafeArrayAccessData(array.parray, (void**)&parrayData);
	LONG ubound, lbound;
	SafeArrayGetLBound(array.parray, 1, &lbound);
	SafeArrayGetUBound(array.parray, 1, &ubound);
	size = ubound - lbound;
	return parrayData;
}

void storageForPlugins::Initialize()
{
	SysFreeString(m_bstr);
	m_bstr = NULL;
	VariantClear(&m_array);
	m_tempFilenameDst = _T("");
}

void storageForPlugins::SetDataFileAnsi(const String& filename, bool bOverwrite /*= false*/) 
{
	m_filename = filename;
	m_nChangedValid = 0;
	m_nChanged = 0;
	m_bOriginalIsUnicode = false;
	m_bCurrentIsUnicode = false;
	m_bCurrentIsFile = true;
	m_bOverwriteSourceFile = bOverwrite;
	m_codepage = ucr::getDefaultCodepage();
	Initialize();
}
void storageForPlugins::SetDataFileUnicode(const String& filename, bool bOverwrite /*= false*/)
{
	m_filename = filename;
	m_nChangedValid = 0;
	m_nChanged = 0;
	m_bOriginalIsUnicode = true;
	m_bCurrentIsUnicode = true;
	m_bCurrentIsFile = true;
	m_bOverwriteSourceFile = bOverwrite;
	m_codepage = ucr::getDefaultCodepage();
	Initialize();
}
void storageForPlugins::SetDataFileUnknown(const String& filename, bool bOverwrite /*= false*/) 
{
	bool bIsUnicode = false;
	UniMemFile ufile;
	if (ufile.OpenReadOnly(filename))
	{
		bIsUnicode = ufile.IsUnicode();
		ufile.Close();
	}
	if (bIsUnicode)
		SetDataFileUnicode(filename, bOverwrite);
	else
		SetDataFileAnsi(filename, bOverwrite);
}

const TCHAR *storageForPlugins::GetDestFileName()
{
	if (m_tempFilenameDst.empty())
	{
		m_tempFilenameDst = env_GetTempFileName(env_GetTempPath(), _T ("_WM"));
	}
	return m_tempFilenameDst.c_str();
}


void storageForPlugins::ValidateNewFile()
{
	// changed data are : file, nChanged
	// nChanged passed as pointer so already upToDate
	// now update file
	if (m_nChangedValid == m_nChanged)
	{
		// plugin succeeded, but nothing changed, just delete the new file
		try
		{
			TFile(m_tempFilenameDst).remove();
		}
		catch (Exception& e)
		{
			LogErrorStringUTF8(e.displayText());
		}
		// we may reuse the temp filename
		// tempFilenameDst.Empty();
	}
	else
	{
		m_nChangedValid = m_nChanged;
		if (m_bOverwriteSourceFile)
		{
			try
			{
				TFile(m_filename).remove();
				TFile(m_tempFilenameDst).renameTo(m_filename);
			}
			catch (Exception& e)
			{
				LogErrorStringUTF8(e.displayText());
			}
		}
		else
		{
			// do not delete the original file name
			m_filename = m_tempFilenameDst;
			// for next transformation, we may overwrite/delete the source file
			m_bOverwriteSourceFile = true;
		}
		m_tempFilenameDst.erase();
	}
}
void storageForPlugins::ValidateNewBuffer()
{
	// changed data are : buffer, nChanged
	// passed as pointers so already upToDate
	m_nChangedValid = m_nChanged;
}

////////////////////////////////////////////////////////////////////////////////

void storageForPlugins::ValidateInternal(bool bNewIsFile, bool bNewIsUnicode)
{
	assert (m_bCurrentIsFile != bNewIsFile || m_bCurrentIsUnicode != bNewIsUnicode);

	// if we create a file, we remove the remaining previous file 
	if (bNewIsFile)
	{
		if (m_bOverwriteSourceFile)
		{
			try
			{
				TFile(m_filename).remove();
				TFile(m_tempFilenameDst).renameTo(m_filename);
			}
			catch (...)
			{
			}
		}
		else
		{
			// do not delete the original file name
			m_filename = m_tempFilenameDst;
			// for next transformation, we may overwrite/delete the source file
			m_bOverwriteSourceFile = true;
		}
		m_tempFilenameDst.erase();
	}

	// old memory structures are freed
	if (!m_bCurrentIsFile)
		// except if the old data have been in situ replaced by new ones
		if (bNewIsFile || m_bCurrentIsUnicode != bNewIsUnicode)
		{
			if (m_bCurrentIsUnicode)
			{
				SysFreeString(m_bstr);
				m_bstr = NULL;
			}
			else
				VariantClear(&m_array);
		}

	m_bCurrentIsUnicode = bNewIsUnicode;
	m_bCurrentIsFile = bNewIsFile;
}

const TCHAR *storageForPlugins::GetDataFileUnicode()
{
	if (m_bCurrentIsFile && m_bCurrentIsUnicode)
		return m_filename.c_str();

	unsigned nchars;
	char * pchar = NULL;
	wchar_t * pwchar = NULL;

	SharedMemory *pshmIn = NULL;
	try
	{
		// Get source data
		if (m_bCurrentIsFile)
		{
			// Init filedata struct and open file as memory mapped (in file)
			TFile fileIn(m_filename);
			pshmIn = new SharedMemory(fileIn, SharedMemory::AM_READ);
			pchar = pshmIn->begin();
			nchars = pshmIn->end() - pshmIn->begin();
		}
		else
		{
			if (m_bCurrentIsUnicode)
			{
				pwchar = m_bstr;
				nchars = SysStringLen(m_bstr);
			}
			else
			{
				pchar = (char *)GetVariantArrayData(m_array, nchars);
			}
		}

		// Compute the dest size (in bytes)
		int textForeseenSize = nchars * sizeof(wchar_t) + 6; // from unicoder.cpp maketstring
		int textRealSize = textForeseenSize;

		// Init filedata struct and open file as memory mapped (out file)
		GetDestFileName();

		TFile fileOut(m_tempFilenameDst);
		fileOut.setSize(textForeseenSize + 2);
		{
			SharedMemory shmOut(fileOut, SharedMemory::AM_WRITE);
			int bom_bytes = ucr::writeBom(shmOut.begin(), ucr::UCS2LE);
			if (m_bCurrentIsUnicode)
			{
				std::memcpy(shmOut.begin()+bom_bytes, pwchar, nchars * sizeof(wchar_t));
				textRealSize = nchars * sizeof(wchar_t);
			}
			else
			{
				// Ansi to UCS-2 conversion, from unicoder.cpp maketstring
				DWORD flags = 0;
				textRealSize = MultiByteToWideChar(m_codepage, flags, pchar, nchars, 
					(wchar_t*)(shmOut.begin()+bom_bytes), textForeseenSize-1)
								* sizeof(wchar_t);
			}
		}
		// size may have changed
		fileOut.setSize(textRealSize + 2);

		// Release pointers to source data
		delete pshmIn;
		if (!m_bCurrentIsFile && !m_bCurrentIsUnicode)
			SafeArrayUnaccessData(m_array.parray);

		if ((textRealSize == 0) && (textForeseenSize > 0))
		{
			// conversion error
			try { TFile(m_tempFilenameDst).remove(); } catch (...) {}
			return NULL;
		}

		ValidateInternal(true, true);
		return m_filename.c_str();
	}
	catch (...)
	{
		delete pshmIn;
		return NULL;
	}
}


BSTR * storageForPlugins::GetDataBufferUnicode()
{
	if (!m_bCurrentIsFile && m_bCurrentIsUnicode)
		return &m_bstr;

	unsigned nchars;
	char * pchar;
	wchar_t * pwchar;
	SharedMemory *pshmIn = NULL;

	try
	{
		// Get source data
		if (m_bCurrentIsFile) 
		{
			// Init filedata struct and open file as memory mapped (in file)
			TFile fileIn(m_filename);
			pshmIn = new SharedMemory(fileIn, SharedMemory::AM_READ);

			if (m_bCurrentIsUnicode)
			{
				pwchar = (wchar_t*) (pshmIn->begin()+2); // pass the BOM
				nchars = (pshmIn->end()-pshmIn->begin()-2) / 2;
			}
			else
			{
				pchar = pshmIn->begin();
				nchars = pshmIn->end()-pshmIn->begin();
			}
		}
		else
		{
			pchar = (char *)GetVariantArrayData(m_array, nchars);
		}

		// Compute the dest size (in bytes)
		int textForeseenSize = nchars * sizeof(wchar_t) + 6; // from unicoder.cpp maketstring
		int textRealSize = textForeseenSize;

		// allocate the memory
		boost::scoped_array<wchar_t> tempBSTR(new wchar_t[textForeseenSize]);

		// fill in the data
		wchar_t * pbstrBuffer = tempBSTR.get();
		bool bAllocSuccess = (pbstrBuffer != NULL);
		if (bAllocSuccess)
		{
			if (m_bCurrentIsUnicode)
			{
				std::memcpy(pbstrBuffer, pwchar, nchars * sizeof(wchar_t));
				textRealSize = nchars * sizeof(wchar_t);
			}
			else
			{
				// Ansi to UCS-2 conversion, from unicoder.cpp maketstring
				DWORD flags = 0;
				textRealSize = MultiByteToWideChar(m_codepage, flags, pchar,
					nchars, pbstrBuffer, textForeseenSize-1) * sizeof(wchar_t);
			}
			SysFreeString(m_bstr);
			m_bstr = SysAllocStringLen(tempBSTR.get(), textRealSize);
			if (!m_bstr)
				bAllocSuccess = false;
		}

		// Release pointers to source data
		delete pshmIn;
		if (!m_bCurrentIsFile && !m_bCurrentIsUnicode)
			SafeArrayUnaccessData(m_array.parray);

		if (!bAllocSuccess)
			return NULL;

		ValidateInternal(false, true);
		return &m_bstr;
	}
	catch (...)
	{
		delete pshmIn;
		return NULL;
	}
}

const TCHAR *storageForPlugins::GetDataFileAnsi()
{
	if (m_bCurrentIsFile && !m_bCurrentIsUnicode)
		return m_filename.c_str();

	unsigned nchars;
	char * pchar;
	wchar_t * pwchar;
	SharedMemory *pshmIn = NULL;

	try
	{
		// Get source data
		if (m_bCurrentIsFile)
		{
			// Init filedata struct and open file as memory mapped (in file)
			TFile fileIn(m_filename);
			pshmIn = new SharedMemory(fileIn, SharedMemory::AM_READ);

			pwchar = (wchar_t*) (pshmIn->begin()+2); // pass the BOM
			nchars = (pshmIn->end()-pshmIn->begin()-2) / 2;
		}
		else 
		{
			if (m_bCurrentIsUnicode)
			{
				pwchar = m_bstr;
				nchars = SysStringLen(m_bstr);
			}
			else
			{
				pchar = (char *)GetVariantArrayData(m_array, nchars);
			}
		}

		// Compute the dest size (in bytes)
		int textForeseenSize = nchars; 
		if (m_bCurrentIsUnicode)
			textForeseenSize = nchars * 3; // from unicoder.cpp convertToBuffer
		int textRealSize = textForeseenSize;

		// Init filedata struct and open file as memory mapped (out file)
		GetDestFileName();
		TFile fileOut(m_tempFilenameDst);
		fileOut.setSize(textForeseenSize);
		{
			SharedMemory shmOut(fileOut, SharedMemory::AM_WRITE);

			if (m_bCurrentIsUnicode)
			{
				// UCS-2 to Ansi conversion, from unicoder.cpp convertToBuffer
				DWORD flags = 0;
				textRealSize = WideCharToMultiByte(m_codepage, flags, pwchar, nchars,
					shmOut.begin(), textForeseenSize, NULL, NULL);
			}
			else
			{
				std::memcpy(shmOut.begin(), pchar, nchars);
			}
		}
		// size may have changed
		fileOut.setSize(textRealSize);

		// Release pointers to source data
		delete pshmIn;
		if (!m_bCurrentIsFile && !m_bCurrentIsUnicode)
			SafeArrayUnaccessData(m_array.parray);

		if ((textRealSize == 0) && (textForeseenSize > 0))
		{
			// conversion error
			try { TFile(m_tempFilenameDst).remove(); } catch (...) {}
			return NULL;
		}

		ValidateInternal(true, false);
		return m_filename.c_str();
	}
	catch (...)
	{
		delete pshmIn;
		return NULL;
	}
}


VARIANT * storageForPlugins::GetDataBufferAnsi()
{
	if (!m_bCurrentIsFile && !m_bCurrentIsUnicode)
		return &m_array;

	unsigned nchars;
	char * pchar;
	wchar_t * pwchar;
	SharedMemory *pshmIn = NULL;

	try
	{
		// Get source data
		if (m_bCurrentIsFile) 
		{
			// Init filedata struct and open file as memory mapped (in file)
			TFile fileIn(m_filename);
			pshmIn = new SharedMemory(fileIn, SharedMemory::AM_READ);

			if (m_bCurrentIsUnicode)
			{
				pwchar = (wchar_t*) (pshmIn->begin()+2); // pass the BOM
				nchars = (pshmIn->end()-pshmIn->begin()-2) / 2;
			}
			else
			{
				pchar = pshmIn->begin();
				nchars = pshmIn->end()-pshmIn->begin();
			}
		}
		else
		{
			pwchar = m_bstr;
			nchars = SysStringLen(m_bstr);
		}

		// Compute the dest size (in bytes)
		int textForeseenSize = nchars; 
		if (m_bCurrentIsUnicode)
			textForeseenSize = nchars * 3; // from unicoder.cpp convertToBuffer
		int textRealSize = textForeseenSize;

		// allocate the memory
		SAFEARRAYBOUND rgsabound = {textForeseenSize, 0};
		m_array.vt = VT_UI1 | VT_ARRAY;
		m_array.parray = SafeArrayCreate(VT_UI1, 1, &rgsabound);
		char * parrayData;
		SafeArrayAccessData(m_array.parray, (void**)&parrayData);

		// fill in the data
		if (m_bCurrentIsUnicode)
		{
			// UCS-2 to Ansi conversion, from unicoder.cpp convertToBuffer
			DWORD flags = 0;
			textRealSize = WideCharToMultiByte(m_codepage, flags, pwchar, nchars, parrayData, textForeseenSize, NULL, NULL);
		}
		else
		{
			std::memcpy(parrayData, pchar, nchars);
		}
		// size may have changed
		SafeArrayUnaccessData(m_array.parray);
		SAFEARRAYBOUND rgsaboundnew = {textRealSize, 0};
		SafeArrayRedim(m_array.parray, &rgsaboundnew);

		// Release pointers to source data
		delete pshmIn;

		ValidateInternal(false, false);
		return &m_array;
	}
	catch (...)
	{
		delete pshmIn;
		return NULL;
	}
}

template<typename T, bool flipbytes>
inline const T *findNextLine(const T *pstart, const T *pend)
{
	for (const T *p = pstart; p < pend; ++p)
	{
		int ch = flipbytes ? ByteOrder::flipBytes(*p) : *p;
		if (ch == '\n')
			return p + 1;
		else if (ch == '\r')
		{
			if (p + 1 < pend && *(p + 1) == (flipbytes ? ByteOrder::flipBytes('\n') : '\n'))
				return p + 2;
			else
				return p + 1;
		}
	}
	return pend;
}

static const char *findNextLine(ucr::UNICODESET unicoding, const char *pstart, const char *pend)
{
	switch (unicoding)
	{
	case ucr::UCS2LE:
		return (const char *)findNextLine<unsigned short, false>((const unsigned short *)pstart, (const unsigned short *)pend);
	case ucr::UCS2BE:
		return (const char *)findNextLine<unsigned short, true>((const unsigned short *)pstart, (const unsigned short *)pend);
	default:
		return findNextLine<char, false>(pstart, pend);
	}
	return pend;
}

bool AnyCodepageToUTF8(int codepage, const String& filepath, const String& filepathDst, int & nFileChanged, bool bWriteBOM)
{
	UniMemFile ufile;
	if (!ufile.OpenReadOnly(filepath))
		return true;
	ufile.ReadBom();
	ucr::UNICODESET unicoding = ufile.GetUnicoding();
	// Finished with examing file contents
	ufile.Close();

	try
	{
		// Init filedataIn struct and open file as memory mapped (input)
		TFile fileIn(filepath);
		SharedMemory shmIn(fileIn, SharedMemory::AM_READ);

		IExconverter *pexconv = Exconverter::getInstance();

		char * pszBuf = shmIn.begin();
		size_t nBufSize = shmIn.end() - shmIn.begin();
		size_t nSizeOldBOM = 0;
		switch (unicoding)
		{
		case ucr::UTF8:
			nSizeOldBOM = 3;
			break;
		case ucr::UCS2LE:
		case ucr::UCS2BE:
			nSizeOldBOM = 2;
			break;
		}

		// first pass : get the size of the destination file
		size_t nSizeBOM = (bWriteBOM) ? 3 : 0;
		size_t nDstSize = nBufSize * 2;

		const size_t minbufsize = 128 * 1024;

		// create the destination file
		FileOutputStream fout(ucr::toUTF8(filepathDst), std::ios::out|std::ios::binary|std::ios::trunc);
		Buffer<char> obuf(minbufsize);
		Int64 pos = nSizeOldBOM;

		// write BOM
		if (bWriteBOM)
		{
			char bom[4];
			fout.write(bom, ucr::writeBom(bom, ucr::UTF8));
		}

		// write data
		for (;;)
		{
			size_t srcbytes = findNextLine(unicoding, pszBuf + pos + minbufsize, pszBuf + nBufSize) - (pszBuf + pos);
			if (srcbytes == 0)
				break;
			if (srcbytes > obuf.size())
				obuf.resize(srcbytes * 2, false);
			size_t destbytes = obuf.size();
			if (pexconv)
			{
				size_t srcbytes2 = srcbytes;
				pexconv->convert(codepage, CP_UTF8, (const unsigned char *)pszBuf+pos, &srcbytes2, (unsigned char *)obuf.begin(), &destbytes);
			}
			else
			{
				bool lossy = false;
				destbytes = ucr::CrossConvert((const char *)pszBuf+pos, srcbytes, obuf.begin(), destbytes, codepage, CP_UTF8, &lossy);
			}
			fout.write(obuf.begin(), destbytes);
			pos += srcbytes;
		}

		nFileChanged ++;
		return true;
	}
	catch (...)
	{
		return false;
	}
}

