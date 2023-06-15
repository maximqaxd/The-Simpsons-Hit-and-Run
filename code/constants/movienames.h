//=============================================================================
// Copyright (C) 2002 Radical Entertainment Ltd.  All rights reserved.
//
// File:        movieconsts.h
//
// Description: Blahblahblah
//
// History:     06/06/2002 + Created -- NAME
//
//=============================================================================

#ifndef MOVIENAMES_H
#define MOVIENAMES_H

//========================================
// Nested Includes
//========================================

//========================================
// Forward References
//========================================

//=============================================================================
//
// Synopsis:    Blahblahblah
//
//=============================================================================

namespace MovieNames
{
#ifdef RAD_XBOX

    static char* DEMO = "D:\\movies\\demo.rmv";
    static char* RADICALLOGO = "D:\\movies\\radlogo.rmv";
    static char* FOXLOGO = "D:\\movies\\foxlogo.rmv";
	static char* GRACIELOGO = "D:\\movies\\gracie.rmv";
    static char* VUGLOGO = "D:\\movies\\vuglogo.rmv";
    static char* INTROFMV = "D:\\movies\\intro.rmv";
    static char* MOVIE1 = "D:\\movies\\fmv1a.rmv";
    static char* MOVIE2 = "D:\\movies\\fmv2.rmv";
    static char* MOVIE3 = "D:\\movies\\fmv3.rmv";
    static char* MOVIE4 = "D:\\movies\\fmv4.rmv";
    static char* MOVIE5 = "D:\\movies\\fmv5.rmv";
    static char* MOVIE6 = "D:\\movies\\fmv6.rmv";
    static char* MOVIE7 = "D:\\movies\\fmv7.rmv";
    static char* MOVIE8 = "D:\\movies\\fmv8.rmv";

#elif defined RAD_PS2

    static char* DEMO = "movies\\demo.rmv";
    static char* RADICALLOGO = "movies\\radlogo.rmv";
    static char* FOXLOGO = "movies\\foxlogo.rmv";
	static char* GRACIELOGO = "movies\\gracie.rmv";
    static char* VUGLOGO = "movies\\vuglogo.rmv";
    static char* INTROFMV = "movies\\intro.rmv";
    static char* MOVIE1 = "movies\\fmv1a.rmv";
    static char* MOVIE2 = "movies\\fmv2.rmv";
    static char* MOVIE3 = "movies\\fmv3.rmv";
    static char* MOVIE4 = "movies\\fmv4.rmv";
    static char* MOVIE5 = "movies\\fmv5.rmv";
    static char* MOVIE6 = "movies\\fmv6.rmv";
    static char* MOVIE7 = "movies\\fmv7.rmv";
    static char* MOVIE8 = "movies\\fmv8.rmv";

#elif defined RAD_GAMECUBE

    static char* DEMO = "movies/demo.rmv";
    static char* RADICALLOGO = "movies/radlogo.rmv";
    static char* FOXLOGO = "movies/foxlogo.rmv";
	static char* GRACIELOGO = "movies/gracie.rmv";
    static char* VUGLOGO = "movies/vuglogo.rmv";
    static char* INTROFMV = "movies/intro.rmv";
    static char* MOVIE1 = "movies/fmv1a.rmv";
    static char* MOVIE2 = "movies/fmv2.rmv";
    static char* MOVIE3 = "movies/fmv3.rmv";
    static char* MOVIE4 = "movies/fmv4.rmv";
    static char* MOVIE5 = "movies/fmv5.rmv";
    static char* MOVIE6 = "movies/fmv6.rmv";
    static char* MOVIE7 = "movies/fmv7.rmv";
    static char* MOVIE8 = "movies/fmv8.rmv";

#elif defined RAD_WIN32

    //static char* DEMO = "movies\\demo.bk2";
    static char* RADICALLOGO = "movies\\radlogo.bk2";
    static char* FOXLOGO = "movies\\foxlogo.bk2";
    static char* GRACIELOGO = "movies\\gracie.bk2";
    static char* VUGLOGO = "movies\\vuglogo.bk2";
    static char* INTROFMV = "movies\\intro.bk2";
    static char* MOVIE1 = "movies\\fmv1a.bk2";
    static char* MOVIE2 = "movies\\fmv2.bk2";
    static char* MOVIE3 = "movies\\fmv3.bk2";
    static char* MOVIE4 = "movies\\fmv4.bk2";
    static char* MOVIE5 = "movies\\fmv5.bk2";
    static char* MOVIE6 = "movies\\fmv6.bk2";
    static char* MOVIE7 = "movies\\fmv7.bk2";
    static char* MOVIE8 = "movies\\fmv8.bk2";

#endif
}

#endif //MOVIENAMES_H
