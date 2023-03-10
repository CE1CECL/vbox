#!/usr/bin/env python
# -*- coding: utf-8 -*-
# $Id$
# pylint: disable=invalid-name

"""
Annotates and generates threaded functions from IEMAllInstructions*.cpp.h.
"""

from __future__ import print_function;

__copyright__ = \
"""
Copyright (C) 2023 Oracle and/or its affiliates.

This file is part of VirtualBox base platform packages, as
available from https://www.virtualbox.org.

This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License
as published by the Free Software Foundation, in version 3 of the
License.

This program is distributed in the hope that it will be useful, but
WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, see <https://www.gnu.org/licenses>.

SPDX-License-Identifier: GPL-3.0-only
"""
__version__ = "$Revision$"

# Standard python imports.
import datetime;
import os;
import sys;
import argparse;

import IEMAllInstructionsPython as iai;


# Python 3 hacks:
if sys.version_info[0] >= 3:
    long = int;     # pylint: disable=redefined-builtin,invalid-name


class ThreadedFunction(object):
    """
    A threaded function.
    """

    def __init__(self, oMcBlock):
        self.oMcBlock               = oMcBlock  # type: IEMAllInstructionsPython.McBlock
        ## Dictionary of local variables (IEM_MC_LOCAL[_CONST]) and call arguments (IEM_MC_ARG*).
        self.dVariables = {}           # type: dict(str,McStmtVar)
        ###
        #self.aoParams    = []          # type:

    @staticmethod
    def dummyInstance():
        """ Gets a dummy instance. """
        return ThreadedFunction(iai.McBlock('null', 999999999, 999999999, 'nil', 999999999));

    def getIndexName(self):
        sName = self.oMcBlock.sFunction;
        if sName.startswith('iemOp_'):
            sName = sName[len('iemOp_'):];
        if self.oMcBlock.iInFunction == 0:
            return 'kIemThreadedFunc_%s' % ( sName, );
        return 'kIemThreadedFunc_%s_%s' % ( sName, self.oMcBlock.iInFunction, );

    def analyzeFindVariablesAndCallArgs(self, aoStmts):
        """ Scans the statements for MC variables and call arguments. """
        for oStmt in aoStmts:
            if isinstance(oStmt, iai.McStmtVar):
                if oStmt.sVarName in self.dVariables:
                    raise Exception('Variable %s is defined more than once!' % (oStmt.sVarName,));
                self.dVariables[oStmt.sVarName] = oStmt.sVarName;

            # There shouldn't be any variables or arguments declared inside if/
            # else blocks, but scan them too to be on the safe side.
            if isinstance(oStmt, iai.McStmtCond):
                cBefore = len(self.dVariables);
                self.analyzeFindVariablesAndCallArgs(oStmt.aoIfBranch);
                self.analyzeFindVariablesAndCallArgs(oStmt.aoElseBranch);
                if len(self.dVariables) != cBefore:
                    raise Exception('Variables/arguments defined in conditional branches!');
        return True;

    def analyze(self):
        """
        Analyzes the code, identifying the number of parameters it requires and such.
        May raise exceptions if we cannot grok the code.
        """

        # Decode the block into a list/tree of McStmt objects.
        aoStmts = self.oMcBlock.decode();

        # Scan the statements for local variables and call arguments (self.dVariables).
        self.analyzeFindVariablesAndCallArgs(aoStmts);


        return True;


    def generateInputCode(self):
        """
        Modifies the input code.
        """
        assert len(self.oMcBlock.asLines) > 2, "asLines=%s" % (self.oMcBlock.asLines,);
        cchIndent = (self.oMcBlock.cchIndent + 3) // 4 * 4;
        return iai.McStmt.renderCodeForList(self.oMcBlock.aoStmts, cchIndent = cchIndent).replace('\n', ' /* gen */\n', 1);


class IEMThreadedGenerator(object):
    """
    The threaded code generator & annotator.
    """

    def __init__(self):
        self.aoThreadedFuncs = []       # type: list(ThreadedFunction)
        self.oOptions        = None     # type: argparse.Namespace
        self.aoParsers       = []       # type: list(IEMAllInstructionsPython.SimpleParser)

    #
    # Processing.
    #

    def processInputFiles(self):
        """
        Process the input files.
        """

        # Parse the files.
        self.aoParsers = iai.parseFiles(self.oOptions.asInFiles);

        # Wrap MC blocks into threaded functions and analyze these.
        self.aoThreadedFuncs = [ThreadedFunction(oMcBlock) for oMcBlock in iai.g_aoMcBlocks];
        for oThreadedFunction in self.aoThreadedFuncs:
            oThreadedFunction.analyze();

        return True;

    #
    # Output
    #

    def generateLicenseHeader(self):
        """
        Returns the lines for a license header.
        """
        return [
            '/*',
            ' * Autogenerated by $Id$ ',
            ' * Do not edit!',
            ' */',
            '',
            '/*',
            ' * Copyright (C) 2023-' + str(datetime.date.today().year) + ' Oracle and/or its affiliates.',
            ' *',
            ' * This file is part of VirtualBox base platform packages, as',
            ' * available from https://www.virtualbox.org.',
            ' *',
            ' * This program is free software; you can redistribute it and/or',
            ' * modify it under the terms of the GNU General Public License',
            ' * as published by the Free Software Foundation, in version 3 of the',
            ' * License.',
            ' *',
            ' * This program is distributed in the hope that it will be useful, but',
            ' * WITHOUT ANY WARRANTY; without even the implied warranty of',
            ' * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU',
            ' * General Public License for more details.',
            ' *',
            ' * You should have received a copy of the GNU General Public License',
            ' * along with this program; if not, see <https://www.gnu.org/licenses>.',
            ' *',
            ' * The contents of this file may alternatively be used under the terms',
            ' * of the Common Development and Distribution License Version 1.0',
            ' * (CDDL), a copy of it is provided in the "COPYING.CDDL" file included',
            ' * in the VirtualBox distribution, in which case the provisions of the',
            ' * CDDL are applicable instead of those of the GPL.',
            ' *',
            ' * You may elect to license modified versions of this file under the',
            ' * terms and conditions of either the GPL or the CDDL or both.',
            ' *',
            ' * SPDX-License-Identifier: GPL-3.0-only OR CDDL-1.0',
            ' */',
            '',
            '',
            '',
        ];


    def generateThreadedFunctionsHeader(self, oOut):
        """
        Generates the threaded functions header file.
        Returns success indicator.
        """

        asLines = self.generateLicenseHeader();

        # Generate the threaded function table indexes.
        asLines += [
            'typedef enum IEMTHREADEDFUNCS',
            '{',
            '    kIemThreadedFunc_Invalid = 0,',
        ];
        for oFunc in self.aoThreadedFuncs:
            asLines.append('    ' + oFunc.getIndexName() + ',');
        asLines += [
            '    kIemThreadedFunc_End',
            '} IEMTHREADEDFUNCS;',
            '',
        ];

        # Prototype the function table.
        asLines += [
            'extern const PFNRT g_apfnIemThreadedFunctions[kIemThreadedFunc_End];',
        ];

        oOut.write('\n'.join(asLines));
        return True;

    def generateThreadedFunctionsSource(self, oOut):
        """
        Generates the threaded functions source file.
        Returns success indicator.
        """

        asLines = self.generateLicenseHeader();

        oOut.write('\n'.join(asLines));
        return True;

    def getThreadedFunctionByIndex(self, idx):
        """
        Returns a ThreadedFunction object for the given index.  If the index is
        out of bounds, a dummy is returned.
        """
        if idx < len(self.aoThreadedFuncs):
            return self.aoThreadedFuncs[idx];
        return ThreadedFunction.dummyInstance();

    def findEndOfMcEndStmt(self, sLine, offEndStmt):
        """
        Helper that returns the line offset following the 'IEM_MC_END();'.
        """
        assert sLine[offEndStmt:].startswith('IEM_MC_END');
        off = sLine.find(';', offEndStmt + len('IEM_MC_END'));
        assert off > 0, 'sLine="%s"' % (sLine, );
        return off + 1 if off > 0 else 99999998;

    def generateModifiedInput(self, oOut):
        """
        Generates the combined modified input source/header file.
        Returns success indicator.
        """
        #
        # File header.
        #
        oOut.write('\n'.join(self.generateLicenseHeader()));

        #
        # ASSUMING that g_aoMcBlocks/self.aoThreadedFuncs are in self.aoParsers
        # order, we iterate aoThreadedFuncs in parallel to the lines from the
        # parsers and apply modifications where required.
        #
        iThreadedFunction = 0;
        oThreadedFunction = self.getThreadedFunctionByIndex(0);
        for oParser in self.aoParsers: # IEMAllInstructionsPython.SimpleParser
            oOut.write("\n\n/* ****** BEGIN %s ******* */\n" % (oParser.sSrcFile,));

            iLine = 0;
            while iLine < len(oParser.asLines):
                sLine = oParser.asLines[iLine];
                iLine += 1;                 # iBeginLine and iEndLine are 1-based.

                # Can we pass it thru?
                if (   iLine not in [oThreadedFunction.oMcBlock.iBeginLine, oThreadedFunction.oMcBlock.iEndLine]
                    or oThreadedFunction.oMcBlock.sSrcFile != oParser.sSrcFile):
                    oOut.write(sLine);
                #
                # Single MC block.  Just extract it and insert the replacement.
                #
                elif oThreadedFunction.oMcBlock.iBeginLine != oThreadedFunction.oMcBlock.iEndLine:
                    assert sLine.count('IEM_MC_') == 1;
                    oOut.write(sLine[:oThreadedFunction.oMcBlock.offBeginLine]);
                    sModified = oThreadedFunction.generateInputCode().strip();
                    oOut.write(sModified);

                    iLine = oThreadedFunction.oMcBlock.iEndLine;
                    sLine = oParser.asLines[iLine - 1];
                    assert sLine.count('IEM_MC_') == 1;
                    oOut.write(sLine[self.findEndOfMcEndStmt(sLine, oThreadedFunction.oMcBlock.offEndLine) : ]);

                    # Advance
                    iThreadedFunction += 1;
                    oThreadedFunction  = self.getThreadedFunctionByIndex(iThreadedFunction);
                #
                # Macro expansion line that have sublines and may contain multiple MC blocks.
                #
                else:
                    offLine = 0;
                    while iLine == oThreadedFunction.oMcBlock.iBeginLine:
                        oOut.write(sLine[offLine : oThreadedFunction.oMcBlock.offBeginLine]);

                        sModified = oThreadedFunction.generateInputCode().strip();
                        assert sModified.startswith('IEM_MC_BEGIN'), 'sModified="%s"' % (sModified,);
                        oOut.write(sModified);

                        offLine = self.findEndOfMcEndStmt(sLine, oThreadedFunction.oMcBlock.offEndLine);

                        # Advance
                        iThreadedFunction += 1;
                        oThreadedFunction  = self.getThreadedFunctionByIndex(iThreadedFunction);

                    # Last line segment.
                    if offLine < len(sLine):
                        oOut.write(sLine[offLine : ]);

            oOut.write("/* ****** END %s ******* */\n" % (oParser.sSrcFile,));

        return True;

    #
    # Main
    #

    def main(self, asArgs):
        """
        C-like main function.
        Returns exit code.
        """

        #
        # Parse arguments
        #
        sScriptDir = os.path.dirname(__file__);
        oParser = argparse.ArgumentParser(add_help = False);
        oParser.add_argument('asInFiles',       metavar = 'input.cpp.h',        nargs = '*',
                             default = [os.path.join(sScriptDir, asFM[0]) for asFM in iai.g_aasAllInstrFilesAndDefaultMap],
                             help = "Selection of VMMAll/IEMAllInstructions*.cpp.h files to use as input.");
        oParser.add_argument('--out-funcs-hdr', metavar = 'file-funcs.h',       dest = 'sOutFileFuncsHdr', action = 'store',
                             default = '-', help = 'The output header file for the functions.');
        oParser.add_argument('--out-funcs-cpp', metavar = 'file-funcs.cpp',     dest = 'sOutFileFuncsCpp', action = 'store',
                             default = '-', help = 'The output C++ file for the functions.');
        oParser.add_argument('--out-mod-input', metavar = 'file-instr.cpp.h',   dest = 'sOutFileModInput', action = 'store',
                             default = '-', help = 'The output C++/header file for the modified input instruction files.');
        oParser.add_argument('--help', '-h', '-?', action = 'help', help = 'Display help and exit.');
        oParser.add_argument('--version', '-V', action = 'version',
                             version = 'r%s (IEMAllThreadedPython.py), r%s (IEMAllInstructionsPython.py)'
                                     % (__version__.split()[1], iai.__version__.split()[1],),
                             help = 'Displays the version/revision of the script and exit.');
        self.oOptions = oParser.parse_args(asArgs[1:]);
        print("oOptions=%s" % (self.oOptions,));

        #
        # Process the instructions specified in the IEM sources.
        #
        if self.processInputFiles():
            #
            # Generate the output files.
            #
            aaoOutputFiles = (
                 ( self.oOptions.sOutFileFuncsHdr, self.generateThreadedFunctionsHeader ),
                 ( self.oOptions.sOutFileFuncsCpp, self.generateThreadedFunctionsSource ),
                 ( self.oOptions.sOutFileModInput, self.generateModifiedInput ),
            );
            fRc = True;
            for sOutFile, fnGenMethod in aaoOutputFiles:
                if sOutFile == '-':
                    fRc = fnGenMethod(sys.stdout) and fRc;
                else:
                    try:
                        oOut = open(sOutFile, 'w');                 # pylint: disable=consider-using-with,unspecified-encoding
                    except Exception as oXcpt:
                        print('error! Failed open "%s" for writing: %s' % (sOutFile, oXcpt,), file = sys.stderr);
                        return 1;
                    fRc = fnGenMethod(oOut) and fRc;
                    oOut.close();
            if fRc:
                return 0;

        return 1;


if __name__ == '__main__':
    sys.exit(IEMThreadedGenerator().main(sys.argv));

