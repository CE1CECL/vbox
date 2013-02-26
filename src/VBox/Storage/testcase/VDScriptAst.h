/** @file
 *
 * VBox HDD container test utility - scripting engine, AST related structures.
 */

/*
 * Copyright (C) 2013 Oracle Corporation
 *
 * This file is part of VirtualBox Open Source Edition (OSE), as
 * available from http://www.virtualbox.org. This file is free software;
 * you can redistribute it and/or modify it under the terms of the GNU
 * General Public License (GPL) as published by the Free Software
 * Foundation, in version 2 as it comes in the "COPYING" file of the
 * VirtualBox OSE distribution. VirtualBox OSE is distributed in the
 * hope that it will be useful, but WITHOUT ANY WARRANTY of any kind.
 */
#ifndef _VDScriptAst_h__
#define _VDScriptAst_h__

#include <iprt/list.h>

/**
 * Position information.
 */
typedef struct VDSRCPOS
{
    /** Line in the source. */
    unsigned       iLine;
    /** Current start character .*/
    unsigned       iChStart;
    /** Current end character. */
    unsigned       iChEnd;
} VDSRCPOS;
/** Pointer to a source position. */
typedef struct VDSRCPOS *PVDSRCPOS;

/**
 * AST node classes.
 */
typedef enum VDSCRIPTASTCLASS
{
    /** Invalid. */
    VDSCRIPTASTCLASS_INVALID = 0,
    /** Function node. */
    VDSCRIPTASTCLASS_FUNCTION,
    /** Function argument. */
    VDSCRIPTASTCLASS_FUNCTIONARG,
    /** Identifier node. */
    VDSCRIPTASTCLASS_IDENTIFIER,
    /** Declaration node. */
    VDSCRIPTASTCLASS_DECLARATION,
    /** Statement node. */
    VDSCRIPTASTCLASS_STATEMENT,
    /** Expression node. */
    VDSCRIPTASTCLASS_EXPRESSION,
    /** @todo: Add if ... else, loops, ... */
    /** 32bit blowup. */
    VDSCRIPTASTCLASS_32BIT_HACK = 0x7fffffff
} VDSCRIPTASTCLASS;
/** Pointer to an AST node class. */
typedef VDSCRIPTASTCLASS *PVDSCRIPTASTCLASS;

/**
 * Core AST structure.
 */
typedef struct VDSCRIPTASTCORE
{
    /** The node class, used for verification. */
    VDSCRIPTASTCLASS enmClass;
    /** List which might be used. */
    RTLISTNODE       ListNode;
    /** Position in the source file of this node. */
    VDSRCPOS         Pos;
} VDSCRIPTASTCORE;
/** Pointer to an AST core structure. */
typedef VDSCRIPTASTCORE *PVDSCRIPTASTCORE;

/** Pointer to an statement node - forward declaration. */
typedef struct VDSCRIPTASTSTMT *PVDSCRIPTASTSTMT;
/** Pointer to an expression node - forward declaration. */
typedef struct VDSCRIPTASTEXPR *PVDSCRIPTASTEXPR;

/**
 * AST identifier node.
 */
typedef struct VDSCRIPTASTIDE
{
    /** Core structure. */
    VDSCRIPTASTCORE    Core;
    /** Number of characters in the identifier, excluding the zero terminator. */
    unsigned           cchIde;
    /** Identifier, variable size. */
    char               aszIde[1];
} VDSCRIPTASTIDE;
/** Pointer to an identifer node. */
typedef VDSCRIPTASTIDE *PVDSCRIPTASTIDE;

/**
 * AST declaration node.
 */
typedef struct VDSCRIPTASTDECL
{
    /** Core structure. */
    VDSCRIPTASTCORE    Core;
    /** @todo */
} VDSCRIPTASTDECL;
/** Pointer to an declaration node. */
typedef VDSCRIPTASTDECL *PVDSCRIPTASTDECL;

/**
 * Expression types.
 */
typedef enum VDSCRIPTEXPRTYPE
{
    /** Invalid. */
    VDSCRIPTEXPRTYPE_INVALID = 0,
    /** Numerical constant. */
    VDSCRIPTEXPRTYPE_PRIMARY_NUMCONST,
    /** String constant. */
    VDSCRIPTEXPRTYPE_PRIMARY_STRINGCONST,
    /** Identifier. */
    VDSCRIPTEXPRTYPE_PRIMARY_IDENTIFIER,
    /** List of assignment expressions. */
    VDSCRIPTEXPRTYPE_ASSIGNMENT_LIST,
    /** Assignment expression. */
    VDSCRIPTEXPRTYPE_ASSIGNMENT,
    /** Postfix increment expression. */
    VDSCRIPTEXPRTYPE_POSTFIX_INCREMENT,
    /** Postfix decrement expression. */
    VDSCRIPTEXPRTYPE_POSTFIX_DECREMENT,
    /** Postfix function call expression. */
    VDSCRIPTEXPRTYPE_POSTFIX_FNCALL,
    /** Unary increment expression. */
    VDSCRIPTEXPRTYPE_UNARY_INCREMENT,
    /** Unary decrement expression. */
    VDSCRIPTEXPRTYPE_UNARY_DECREMENT,
    /** Unary positive sign expression. */
    VDSCRIPTEXPRTYPE_UNARY_POSSIGN,
    /** Unary negtive sign expression. */
    VDSCRIPTEXPRTYPE_UNARY_NEGSIGN,
    /** Unary invert expression. */
    VDSCRIPTEXPRTYPE_UNARY_INVERT,
    /** Unary negate expression. */
    VDSCRIPTEXPRTYPE_UNARY_NEGATE,
    /** Multiplicative expression. */
    VDSCRIPTEXPRTYPE_MULTIPLICATION,
    /** Division expression. */
    VDSCRIPTEXPRTYPE_DIVISION,
    /** Modulus expression. */
    VDSCRIPTEXPRTYPE_MODULUS,
    /** Addition expression. */
    VDSCRIPTEXPRTYPE_ADDITION,
    /** Subtraction expression. */
    VDSCRIPTEXPRTYPE_SUBTRACTION,
    /** Logical shift right. */
    VDSCRIPTEXPRTYPE_LSR,
    /** Logical shift left. */
    VDSCRIPTEXPRTYPE_LSL,
    /** Lower than expression */
    VDSCRIPTEXPRTYPE_LOWER,
    /** Higher than expression */
    VDSCRIPTEXPRTYPE_HIGHER,
    /** Lower or equal than expression */
    VDSCRIPTEXPRTYPE_LOWEREQUAL,
    /** Higher or equal than expression */
    VDSCRIPTEXPRTYPE_HIGHEREQUAL,
    /** Equals expression */
    VDSCRIPTEXPRTYPE_EQUAL,
    /** Not equal expression */
    VDSCRIPTEXPRTYPE_NOTEQUAL,
    /** Bitwise and expression */
    VDSCRIPTEXPRTYPE_BITWISE_AND,
    /** Bitwise xor expression */
    VDSCRIPTEXPRTYPE_BITWISE_XOR,
    /** Bitwise or expression */
    VDSCRIPTEXPRTYPE_BITWISE_OR,
    /** Logical and expression */
    VDSCRIPTEXPRTYPE_LOGICAL_AND,
    /** Logical or expression */
    VDSCRIPTEXPRTYPE_LOGICAL_OR,
    /** Assign expression */
    VDSCRIPTEXPRTYPE_ASSIGN,
    /** Multiplicative assign expression */
    VDSCRIPTEXPRTYPE_ASSIGN_MULT,
    /** Division assign expression */
    VDSCRIPTEXPRTYPE_ASSIGN_DIV,
    /** Modulus assign expression */
    VDSCRIPTEXPRTYPE_ASSIGN_MOD,
    /** Additive assign expression */
    VDSCRIPTEXPRTYPE_ASSIGN_ADD,
    /** Subtractive assign expression */
    VDSCRIPTEXPRTYPE_ASSIGN_SUB,
    /** Bitwise left shift assign expression */
    VDSCRIPTEXPRTYPE_ASSIGN_LSL,
    /** Bitwise right shift assign expression */
    VDSCRIPTEXPRTYPE_ASSIGN_LSR,
    /** Bitwise and assign expression */
    VDSCRIPTEXPRTYPE_ASSIGN_AND,
    /** Bitwise xor assign expression */
    VDSCRIPTEXPRTYPE_ASSIGN_XOR,
    /** Bitwise or assign expression */
    VDSCRIPTEXPRTYPE_ASSIGN_OR,
    /** 32bit hack. */
    VDSCRIPTEXPRTYPE_32BIT_HACK = 0x7fffffff
} VDSCRIPTEXPRTYPE;
/** Pointer to an expression type. */
typedef VDSCRIPTEXPRTYPE *PVDSCRIPTEXPRTYPE;

/**
 * AST expression node.
 */
typedef struct VDSCRIPTASTEXPR
{
    /** Core structure. */
    VDSCRIPTASTCORE    Core;
    /** Expression type. */
    VDSCRIPTEXPRTYPE   enmType;
    /** Expression type dependent data. */
    union
    {
        /** Numerical constant. */
        uint64_t          u64;
        /** Primary identifier. */
        PVDSCRIPTASTIDE   pIde;
        /** String literal */
        const char       *pszStr;
        /** List of expressions - VDSCRIPTASTEXPR. */
        RTLISTANCHOR      ListExpr;
        /** Pointer to another expression. */
        PVDSCRIPTASTEXPR  pExpr;
        /** Function call expression. */
        struct
        {
            /** Other postfix expression used as the identifier for the function. */
            PVDSCRIPTASTEXPR pFnIde;
            /** Argument list if existing. */
            RTLISTANCHOR     ListArgs;
        } FnCall;
        /** Binary operation. */
        struct
        {
            /** Left operator. */
            PVDSCRIPTASTEXPR pLeftExpr;
            /** Right operator. */
            PVDSCRIPTASTEXPR pRightExpr;
        } BinaryOp;
    };
} VDSCRIPTASTEXPR;

/**
 * AST if node.
 */
typedef struct VDSCRIPTASTIF
{
    /** Conditional expression. */
    PVDSCRIPTASTEXPR   pCond;
    /** The true branch */
    PVDSCRIPTASTSTMT   pTrueStmt;
    /** The else branch, can be NULL if no else branch. */
    PVDSCRIPTASTSTMT   pElseStmt;
} VDSCRIPTASTIF;
/** Pointer to an expression node. */
typedef VDSCRIPTASTIF *PVDSCRIPTASTIF;

/**
 * AST switch node.
 */
typedef struct VDSCRIPTASTSWITCH
{
    /** Conditional expression. */
    PVDSCRIPTASTEXPR   pCond;
    /** The statement to follow. */
    PVDSCRIPTASTSTMT   pStmt;
} VDSCRIPTASTSWITCH;
/** Pointer to an expression node. */
typedef VDSCRIPTASTSWITCH *PVDSCRIPTASTSWITCH;

/**
 * AST while or do ... while node.
 */
typedef struct VDSCRIPTASTWHILE
{
    /** Flag whether this is a do while loop. */
    bool               fDoWhile;
    /** Conditional expression. */
    PVDSCRIPTASTEXPR   pCond;
    /** The statement to follow. */
    PVDSCRIPTASTSTMT   pStmt;
} VDSCRIPTASTWHILE;
/** Pointer to an expression node. */
typedef VDSCRIPTASTWHILE *PVDSCRIPTASTWHILE;

/**
 * AST for node.
 */
typedef struct VDSCRIPTASTFOR
{
    /** Initializer expression. */
    PVDSCRIPTASTEXPR   pExprStart;
    /** The exit condition. */
    PVDSCRIPTASTEXPR   pExprCond;
    /** The third expression (normally used to increase/decrease loop variable). */
    PVDSCRIPTASTEXPR   pExpr3;
    /** The for loop body. */
    PVDSCRIPTASTSTMT   pStmt;
} VDSCRIPTASTFOR;
/** Pointer to an expression node. */
typedef VDSCRIPTASTFOR *PVDSCRIPTASTFOR;

/**
 * Statement types.
 */
typedef enum VDSCRIPTSTMTTYPE
{
    /** Invalid. */
    VDSCRIPTSTMTTYPE_INVALID = 0,
    /** Labeled statement. */
    VDSCRIPTSTMTTYPE_LABELED,
    /** Compound statement. */
    VDSCRIPTSTMTTYPE_COMPOUND,
    /** Expression statement. */
    VDSCRIPTSTMTTYPE_EXPRESSION,
    /** if statement. */
    VDSCRIPTSTMTTYPE_IF,
    /** switch statement. */
    VDSCRIPTSTMTTYPE_SWITCH,
    /** while statement. */
    VDSCRIPTSTMTTYPE_WHILE,
    /** for statement. */
    VDSCRIPTSTMTTYPE_FOR,
    /** continue statement. */
    VDSCRIPTSTMTTYPE_CONTINUE,
    /** break statement. */
    VDSCRIPTSTMTTYPE_BREAK,
    /** return statement. */
    VDSCRIPTSTMTTYPE_RETURN,
    /** case statement. */
    VDSCRIPTSTMTTYPE_CASE,
    /** default statement. */
    VDSCRIPTSTMTTYPE_DEFAULT,
    /** 32bit hack. */
    VDSCRIPTSTMTTYPE_32BIT_HACK = 0x7fffffff
} VDSCRIPTSTMTTYPE;
/** Pointer to a statement type. */
typedef VDSCRIPTSTMTTYPE *PVDSCRIPTSTMTTYPE;

/**
 * AST statement node.
 */
typedef struct VDSCRIPTASTSTMT
{
    /** Core structure. */
    VDSCRIPTASTCORE    Core;
    /** Statement type */
    VDSCRIPTSTMTTYPE   enmStmtType;
    /** Statement type dependent data. */
    union
    {
        /** Labeled statement (case, default). */
        struct
        {
            /** Conditional expression, if NULL this is a statement for "default" */
            PVDSCRIPTASTEXPR   pCondExpr;
            /** Statement to execute. */
            PVDSCRIPTASTSTMT   pExec;
        } Labeled;
        /** Compound statement. */
        struct
        {
            /** List of declarations - VDSCRIPTASTDECL. */
            RTLISTANCHOR       ListDecls;
            /** List of statements - VDSCRIPTASTSTMT. */
            RTLISTANCHOR       ListStmts;
        } Compound;
        /** case statement. */
        struct
        {
            /** Pointer to the expression. */
            PVDSCRIPTASTEXPR   pExpr;
            /** Pointer to the statement. */
            PVDSCRIPTASTSTMT   pStmt;
        } Case;
        /** "if" statement. */
        VDSCRIPTASTIF          If;
        /** "switch" statement. */
        VDSCRIPTASTSWITCH      Switch;
        /** "while" or "do ... while" loop. */
        VDSCRIPTASTWHILE       While;
        /** "for" loop. */
        VDSCRIPTASTFOR         For;
        /** Pointer to another statement. */
        PVDSCRIPTASTSTMT       pStmt;
        /** Expression statement. */
        PVDSCRIPTASTEXPR       pExpr;
    };
} VDSCRIPTASTSTMT;

/**
 * AST node for one function argument.
 */
typedef struct VDSCRIPTASTFNARG
{
    /** Core structure. */
    VDSCRIPTASTCORE    Core;
    /** Identifier describing the type of the argument. */
    PVDSCRIPTASTIDE    pType;
    /** The name of the argument. */
    PVDSCRIPTASTIDE    pArgIde;
} VDSCRIPTASTFNARG;
/** Pointer to a AST function argument node. */
typedef VDSCRIPTASTFNARG *PVDSCRIPTASTFNARG;

/**
 * AST node describing a function.
 */
typedef struct VDSCRIPTASTFN
{
    /** Core structure. */
    VDSCRIPTASTCORE      Core;
    /** Identifier describing the return type. */
    PVDSCRIPTASTIDE      pRetType;
    /** Name of the function. */
    PVDSCRIPTASTIDE      pFnIde;
    /** Number of arguments in the list. */
    unsigned             cArgs;
    /** Argument list - VDSCRIPTASTFNARG. */
    RTLISTANCHOR         ListArgs;
    /** Compound statement node. */
    PVDSCRIPTASTSTMT     pCompoundStmts;
} VDSCRIPTASTFN;
/** Pointer to a function AST node. */
typedef VDSCRIPTASTFN *PVDSCRIPTASTFN;

/**
 * Free the given AST node and all subsequent nodes pointed to
 * by the given node.
 *
 * @returns nothing.
 * @param   pAstNode    The AST node to free.
 */
DECLHIDDEN(void) vdScriptAstNodeFree(PVDSCRIPTASTCORE pAstNode);

/**
 * Allocate a non variable in size AST node of the given class.
 *
 * @returns Pointer to the allocated node.
 *          NULL if out of memory.
 * @param   enmClass    The class of the AST node.
 */
DECLHIDDEN(PVDSCRIPTASTCORE) vdScriptAstNodeAlloc(VDSCRIPTASTCLASS enmClass);

/**
 * Allocate a IDE node which can hold the given number of characters.
 *
 * @returns Pointer to the allocated node.
 *          NULL if out of memory.
 * @param   cchIde      Number of characters which can be stored in the node.
 */
DECLHIDDEN(PVDSCRIPTASTIDE) vdScriptAstNodeIdeAlloc(unsigned cchIde);

#endif /* _VDScriptAst_h__ */
