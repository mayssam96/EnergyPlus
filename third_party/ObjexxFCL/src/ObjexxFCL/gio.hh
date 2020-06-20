#ifndef ObjexxFCL_gio_hh_INCLUDED
#define ObjexxFCL_gio_hh_INCLUDED

// Global I/O Support
//
// Project: Objexx Fortran-C++ Library (ObjexxFCL)
//
// Version: 4.2.0
//
// Language: C++
//
// Copyright (c) 2000-2017 Objexx Engineering, Inc. All Rights Reserved.
// Use of this source code or any derivative of it is restricted by license.
// Licensing is available from Objexx Engineering, Inc.:  http://objexx.com

// Notes:
//  String read/write are non-global convenience functions

// ObjexxFCL Headers
#include <ObjexxFCL/gio_Fmt.hh> // Convenience include
#include <ObjexxFCL/Print.hh>
#include <ObjexxFCL/Read.hh>

// C++ Headers
#include <ios>
#include <string>

namespace ObjexxFCL {

// Forward
class IOFlags;

namespace gio {

// Types
typedef  int  Unit;
typedef  std::string  Name;

// Data
extern std::string const LF; // Linefeed

// Unit /////

int
get_unit();

// Open /////

// Open File on Specified Unit
bool
open( Unit const unit, Name const & name, IOFlags & flags );

// Open File on Specified Unit
inline
bool
open( Unit const unit, char const * const name, IOFlags & flags )
{
	return open( unit, std::string( name ), flags );
}

// Open File on Specified Unit
bool
open( Unit const unit, Name const & name, std::ios_base::openmode const mode );

// Open File on Specified Unit
inline
bool
open( Unit const unit, char const * const name, std::ios_base::openmode const mode )
{
	return open( unit, std::string( name ), mode );
}

// Open File on Specified Unit
bool
open( Unit const unit, Name const & name );

// Open File on Specified Unit
inline
bool
open( Unit const unit, char const * const name )
{
	return open( unit, std::string( name ) );
}

// Open Default File on Specified Unit
bool
open( Unit const unit, IOFlags & flags );

// Open Default File on Specified Unit
bool
open( Unit const unit, std::ios_base::openmode const mode );

// Open Default File on Specified Unit
bool
open( Unit const unit );

// Open File and Return Unit
Unit
open( Name const & name, IOFlags & flags );

// Open File and Return Unit
inline
Unit
open( char const * const name, IOFlags & flags )
{
	return open( std::string( name ), flags );
}

// Open File and Return Unit
Unit
open( Name const & name, std::ios_base::openmode const mode );


// Open File and Return Unit
inline
Unit
open( char const * const name, std::ios_base::openmode const mode )
{
	return open( std::string( name ), mode );
}

// Open File and Return Unit
Unit
open( Name const & name );

// Open File and Return Unit
inline
Unit
open( char const * const name )
{
	return open( std::string( name ) );
}

// Open Default File and Return Unit
Unit
open( IOFlags & flags );

// Open Default File and Return Unit
Unit
open();

// Read /////

// Read from Unit
ReadStream
read( Unit const unit, std::string const & fmt, bool const beg = false );

// Read from Unit
ReadStream
read( Unit const unit, Fmt const & fmt, bool const beg = false );

// Read from Unit
ReadStream
read( Unit const unit, Fmt & fmt, bool const beg = false );

// Read from Unit
ReadStream
read( Unit const unit, std::string const & fmt, IOFlags & flags, bool const beg = false );

// Read from Unit
ReadStream
read( Unit const unit, Fmt const & fmt, IOFlags & flags, bool const beg = false );

// Read from Unit
ReadStream
read( Unit const unit, Fmt & fmt, IOFlags & flags, bool const beg = false );

// Read from stdin
ReadStream
read( std::string const & fmt );

// Read from stdin
ReadStream
read( Fmt const & fmt );

// Read from stdin
ReadStream
read( Fmt & fmt );

// Read from stdin
ReadStream
read( std::string const & fmt, IOFlags & flags );

// Read from stdin
ReadStream
read( Fmt const & fmt, IOFlags & flags );

// Read from stdin
ReadStream
read( Fmt & fmt, IOFlags & flags );

// Read from String
inline
ReadString
read( std::string const & str, std::string const & fmt )
{
	return ReadString( str, fmt );
}

// Read from String
inline
ReadString
read( std::string const & str, Fmt const & fmt )
{
	return ReadString( str, fmt );
}

// Read from String
inline
ReadString
read( std::string const & str, Fmt & fmt )
{
	return ReadString( str, fmt );
}

// Read from String
inline
ReadString
read( std::string const & str, std::string const & fmt, IOFlags & flags )
{
	return ReadString( str, fmt, flags );
}

// Read from String
inline
ReadString
read( std::string const & str, Fmt const & fmt, IOFlags & flags )
{
	return ReadString( str, fmt, flags );
}

// Read from String
inline
ReadString
read( std::string const & str, Fmt & fmt, IOFlags & flags )
{
	return ReadString( str, fmt, flags );
}



// Output Stream of Unit
std::ostream *
out_stream( Unit const unit );


// Inquire /////

// Inquire by Unit
void
inquire( Unit const unit, IOFlags & flags );

// Inquire by Name
void
inquire( Name const & name, IOFlags & flags );

// Inquire by Name
void
inquire( char const * const name, IOFlags & flags );

// File Exists?
bool
file_exists( std::string const & file_name );

// Backspace /////

// Backspace
void
backspace( Unit const unit, IOFlags & flags );

// Backspace
void
backspace( Unit const unit );

// Rewind /////

// Rewind
void
rewind( Unit const unit, IOFlags & flags );

// Rewind
void
rewind( Unit const unit );

// Close /////

// Close File
void
close( Unit const unit );

// Close File
void
close( Unit const unit, IOFlags & flags );

} // gio
} // ObjexxFCL

#endif // ObjexxFCL_gio_hh_INCLUDED
