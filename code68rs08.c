/* code68rs08.c */
/*****************************************************************************/
/* AS-Portierung                                                             */
/*                                                                           */
/* Codegenerator 68RS08                                                      */
/*                                                                           */
/* Historie: 2006-06-02 initial version based on code6805.c                  */
/*                                                                           */
/*****************************************************************************/
/* $Id: code68rs08.c,v                                                       */
/*****************************************************************************
 * $Log: code68rs08.c,v $
 * Revision 1.5  2014/11/14 12:24:36  alfred
 * - rework to current style
 *
 * Revision 1.4  2013-03-31 18:06:47  alfred
 * - add missing PADDING instruction
 *
 * Revision 1.3  2007/11/24 22:48:05  alfred
 * - some NetBSD changes
 *
 * Revision 1.2  2006/08/05 18:06:43  alfred
 * - silence some compiler warnings
 *
 * Revision 1.1  2006/07/08 10:32:55  alfred
 * - added RS08
 *
 *****************************************************************************/

#include "stdinc.h"

#include <string.h>
#include <ctype.h>

#include "bpemu.h"
#include "strutil.h"

#include "asmdef.h"
#include "asmpars.h"
#include "asmsub.h"
#include "asmitree.h"
#include "motpseudo.h"
#include "codevars.h"
#include "errmsg.h"

#include "code68rs08.h"

typedef struct
{
  char *Name;
  CPUVar MinCPU;
  Byte Code;
} BaseOrder;

typedef struct 
{
  char *Name;
  CPUVar MinCPU;
  Byte Code;
  Word Mask;
} ALUOrder;

typedef struct 
{
  char *Name;
  CPUVar MinCPU;
  Byte Code;
  Byte DCode;
  Word Mask;
} RMWOrder;

#define FixedOrderCnt 23
#define RelOrderCnt 9
#define ALUOrderCnt 8
#define RMWOrderCnt 7

enum
{
  ModNone = -1,
  ModImm = 0,
  ModDir = 1,
  ModExt = 2,
  ModSrt = 3,
  ModTny = 4,
  ModIX = 5,
  ModX = 6,
};

#define MModImm (1 << ModImm)
#define MModDir (1 << ModDir)
#define MModExt (1 << ModExt)
#define MModSrt (1 << ModSrt)
#define MModTny (1 << ModTny)
#define MModIX (1 << ModIX)
#define MModX (1 << ModX)

static ShortInt AdrMode, OpSize;
static Byte AdrVals[2];

static IntType AdrIntType;

static CPUVar CPU68RS08;

static BaseOrder *FixedOrders;
static BaseOrder *RelOrders;
static RMWOrder *RMWOrders;
static ALUOrder *ALUOrders;

/*--------------------------------------------------------------------------*/
/* address parser */


static char *ChkZero(char *s, Byte *pErg)
{
  if (*s == '<') /* short / tiny */
  {
    *pErg = 2;
    return s + 1;
  }
  else if (*s == '>') /* direct */
  {
    *pErg = 1;
    return  s + 1;
  }
  else /* let the assembler make the choice */
  {
    *pErg = 0;
    return s;
  }
}

static void DecodeAdr(Byte Start, Byte Stop, Word Mask)
{
  Boolean OK;
  Word AdrWord;
  Byte ZeroMode;
  char *s;

  AdrMode = ModNone;
  AdrCnt = 0;

  if (Stop - Start == 1)
  {
    if (*(ArgStr[Start]) == 0 && (!strcasecmp(ArgStr[Stop], "X")))
    {
      AdrMode = ModIX;
    }
    else
    {
      WrXErrorPos(ErrNum_InvReg, ArgStr[Stop], &ArgStrPos[Stop]);
      goto chk;
    }
  }

  else if (Stop == Start)
  {
    /* X-indirekt */

    if (!strcasecmp(ArgStr[Start], "X"))
    {
      AdrMode = ModX;
      goto chk;
    }

    if (!strcasecmp(ArgStr[Start], "D[X]"))
    {
      AdrMode = ModIX;
      goto chk;
    }

    /* immediate */

    if (*ArgStr[Start] == '#')
    {
      AdrVals[0] = EvalIntExpression(ArgStr[Start] + 1, Int8, &OK);
      if (OK)
      {
        AdrCnt = 1;
        AdrMode = ModImm;
      }
      goto chk;
    }

    /* absolut */

    s = ChkZero(ArgStr[Start], &ZeroMode);
    FirstPassUnknown = False;
    AdrWord = EvalIntExpression(s, (ZeroMode == 2) ? UInt8 : AdrIntType, &OK);

    if (OK)
    {
      if (((Mask & MModExt) == 0) || (ZeroMode == 2) || ((ZeroMode == 0) && (Hi(AdrWord) == 0)))
      {
        if (FirstPassUnknown)
          AdrWord &= 0xff;
        if (Hi(AdrWord) != 0) WrError(1340);
        else
        {
          AdrCnt = 1;
          AdrVals[0] = Lo(AdrWord); 
	  AdrMode = ModDir;
	  if (ZeroMode == 0)
	  {
	    if ((Mask & MModTny) && (AdrVals[0] <= 0x0f))
              AdrMode = ModTny;
	    if ((Mask & MModSrt) && (AdrVals[0] <= 0x1f))
              AdrMode = ModSrt;
	  }
	  if (ZeroMode == 2)
	  {
	    if (Mask & MModTny)
	    {
	      if (AdrVals[0] <= 0x0f)
                AdrMode = ModTny;
              else
                WrError(1131);
	      return; 
	    }
            else if (Mask & MModSrt) 
	    {
	      if (AdrVals[0] <= 0x1f)
                AdrMode = ModSrt;
              else
                WrError(1131);
	      return; 
	    }
	    else 
	    {
	      AdrMode = ModNone;
	      WrError(1340);
	      return;
	    }
	  }
        }
      }
      else
      {
        AdrVals[0] = Hi(AdrWord);
        AdrVals[1] = Lo(AdrWord);
        AdrCnt = 2;
        AdrMode = ModExt;
      }
      goto chk;
    }
  }

  else
   (void)ChkArgCnt(Start, Start + 1);

chk:
  if ((AdrMode != ModNone) && (!(Mask & (1 << AdrMode))))
  {
    WrError(1350);
    AdrMode = ModNone;
    AdrCnt = 0;
  }
}

/*--------------------------------------------------------------------------*/
/* instruction parsers */

static void DecodeFixed(Word Index)
{
  BaseOrder *pOrder = FixedOrders + Index;

  if (ChkArgCnt(0, 0)
   && ChkMinCPU(pOrder->MinCPU))
  {
    CodeLen = 1;
    BAsmCode[0] = pOrder->Code;
  }
}

static void DecodeMOV(Word Index)
{
  UNUSED(Index);

  if (ChkArgCnt(2, 2))
  {
    OpSize = eSymbolSize8Bit;
    DecodeAdr(1, 1, MModImm | MModDir | MModIX);
    switch (AdrMode)
    {
      case ModImm:
        BAsmCode[1] = AdrVals[0];
        DecodeAdr(2, 2, MModDir | MModIX);
        switch (AdrMode)
	{
	  case ModDir:
            BAsmCode[0] = 0x3e;
            BAsmCode[2] = AdrVals[0];
            CodeLen = 3;
	    break;
	  case ModIX:
            BAsmCode[0] = 0x3e;
            BAsmCode[2] = 0x0e;
            CodeLen = 3;
	    break;
        }
        break;
      case ModDir:
        BAsmCode[1] = AdrVals[0];
        DecodeAdr(2, 2, MModDir | MModIX);
        switch (AdrMode)
        {
          case ModDir:
            BAsmCode[0] = 0x4e;
            BAsmCode[2] = AdrVals[0];
            CodeLen = 3;
            break;
          case ModIX:
            BAsmCode[0] = 0x4e;
            BAsmCode[2] = 0x0e;
            CodeLen = 3;
            break;
        }
        break;
      case ModIX:
        DecodeAdr(2, 2, MModDir);
        if (AdrMode == ModDir)
        {
          BAsmCode[0] = 0x4e;
          BAsmCode[1] = 0x0e;
          BAsmCode[2] = AdrVals[0];
          CodeLen = 3;
        }
        break;
    }
  }
}

static void DecodeRel(Word Index)
{
  BaseOrder *pOrder = RelOrders + Index;
  Boolean OK;
  LongInt AdrInt;

  if (ChkArgCnt(1, 1)
   && ChkMinCPU(pOrder->MinCPU))
  {
    AdrInt = EvalIntExpression(ArgStr[1], AdrIntType, &OK) - (EProgCounter() + 2);
    if (OK)
    {
      if ((!SymbolQuestionable) && ((AdrInt < -128) || (AdrInt>127))) WrError(1370);
      else
      {
        CodeLen = 2;
        BAsmCode[0] = pOrder->Code;
        BAsmCode[1] = Lo(AdrInt);
	if (BAsmCode[0] == 0x00) /* BRN pseudo op */
	{
	  BAsmCode[0] = 0x30;
	  BAsmCode[1] = 0x00;
	}
      }
    }
  }
}

static void DecodeCBEQx(Word Index)
{
  Boolean OK;
  LongInt AdrInt;

  UNUSED(Index);

  if (ChkArgCnt(2, 2))
  {
    OpSize = eSymbolSize8Bit;
    DecodeAdr(1, 1, MModImm);
    if (AdrMode == ModImm)
    {
      BAsmCode[1] = AdrVals[0];
      AdrInt = EvalIntExpression(ArgStr[2], AdrIntType, &OK) - (EProgCounter() + 3);
      if (OK)
      {
        if ((!SymbolQuestionable) && ((AdrInt < -128) || (AdrInt > 127))) WrError(1370);
        else
        {
          BAsmCode[0] = 0x41;
          BAsmCode[2] = AdrInt & 0xff;
          CodeLen = 3;
        }
      }
    }
  }
}

static void DecodeCBEQ(Word Index)
{
  Boolean OK;
  LongInt AdrInt;

  UNUSED(Index);

  if (ArgCnt == 2)
  {
    DecodeAdr(1, 1, MModDir | MModX);
    switch (AdrMode)
    {
      case ModDir:
        BAsmCode[1] = AdrVals[0];
        break;
      case ModX:
        BAsmCode[1] = 0x0f;
        break;
    }
    if (AdrMode != ModNone)
    {
      BAsmCode[0] = 0x31;
      AdrInt = EvalIntExpression(ArgStr[2], AdrIntType, &OK) - (EProgCounter() + 3);
      if (OK)
      {
        if ((!SymbolQuestionable) && ((AdrInt < -128) || (AdrInt > 127))) WrError(1370);
        else
        {
          BAsmCode[2] = AdrInt & 0xff;
          CodeLen = 3;
        }
      }
    }
  }
  else if (ArgCnt == 3)
  {
    if ((*(ArgStr[1]) != 0) || (strcasecmp(ArgStr[2], "X"))) WrXErrorPos(ErrNum_InvReg, ArgStr[2], &ArgStrPos[2]);
    else
    {
      BAsmCode[0] = 0x31; BAsmCode[1] = 0x0e;
      AdrInt = EvalIntExpression(ArgStr[3], AdrIntType, &OK) - (EProgCounter() + 3);
      if (OK)
      {
        if ((!SymbolQuestionable) && ((AdrInt < -128) || (AdrInt > 127))) WrError(1370);
        else
        {
          BAsmCode[2] = AdrInt & 0xff;
          CodeLen = 3;
        }
      }
    }
  }
  else
    (void)ChkArgCnt(2, 3);
}

static void DecodeDBNZx(Word Index)
{
  Boolean OK;
  LongInt AdrInt;

  if (ChkArgCnt(1, 1))
  {
    AdrInt = EvalIntExpression(ArgStr[1], AdrIntType, &OK) - (EProgCounter() + ((Index == 0) ? 3 : 2));
    if (OK)
    {
      if ((!SymbolQuestionable) && ((AdrInt < -128) || (AdrInt > 127))) WrError(1370);
      else if (Index == 0)
      {
        BAsmCode[0] = 0x3b;
	BAsmCode[1] = 0x0f;
        BAsmCode[2] = AdrInt & 0xff;
        CodeLen = 3;
      }
      else 
      {
        BAsmCode[0] = 0x4b;
        BAsmCode[1] = AdrInt & 0xff;
        CodeLen = 2;
      }
    }
  }
}

static void DecodeDBNZ(Word Index)
{
  Boolean OK;   
  LongInt AdrInt;
  Byte Disp = 0;

  UNUSED(Index);

  if (ChkArgCnt(2, 3))
  {
    DecodeAdr(1, ArgCnt - 1, MModDir | MModIX);
    switch (AdrMode)
    {
      case ModDir:
        BAsmCode[0] = 0x3b;
        BAsmCode[1] = AdrVals[0];
        Disp = 3;
        break;
      case ModIX:
        BAsmCode[0] = 0x3b;
        BAsmCode[1] = 0x0e;
        Disp = 3;
        break;
    }
    if (AdrMode != ModNone)
    {
      AdrInt = EvalIntExpression(ArgStr[ArgCnt], AdrIntType, &OK) - (EProgCounter() + Disp);
      if (OK)
      {
        if ((!SymbolQuestionable) && ((AdrInt < -128) || (AdrInt > 127))) WrError(1370);
        else
        {
          BAsmCode[Disp - 1] = AdrInt & 0xff;
          CodeLen = Disp;
        }
      }
    }
  }
}


static void DecodeLDX(Word Index)
{

  BAsmCode[0] = 0x4e;

  if (ChkArgCnt(1, 2)) 
  {
    DecodeAdr(1, ArgCnt, (Index == 0) ? (MModImm | MModDir | MModIX) : MModDir);
    if (AdrMode != ModNone)
    {
      switch (AdrMode)
      {
        case ModImm:
          BAsmCode[0] = 0x3e;
          BAsmCode[1] = AdrVals[0];
          BAsmCode[2] = 0x0f;
	  break;	
	case ModDir:
	  if (Index == 0)
	  {
            BAsmCode[1] = AdrVals[0];
            BAsmCode[2] = 0x0f;
	  }
	  else
	  {
            BAsmCode[1] = 0x0f;
            BAsmCode[2] = AdrVals[0];
	  }
          break;
	case ModIX:
          BAsmCode[1] = 0x0e;
          BAsmCode[2] = 0x0e;
	  break;
      }
      CodeLen = 3;
    }
  }  
}

static void DecodeTST(Word Index)
{

  BAsmCode[0] = 0x4e;  
  
  if (Index == 1)
  {
    if (ChkArgCnt(0, 0))
    {
      BAsmCode[1] = 0x0f;
      BAsmCode[2] = 0x0f;
      CodeLen = 3;
    }  
  } 
  else if (Index == 2)
  {
    BAsmCode[0] = 0xaa;
    BAsmCode[1] = 0x00;
    CodeLen = 2;
  }
  else
  {
    DecodeAdr(1, ArgCnt, MModDir | MModX | MModIX);
    if (AdrMode != ModNone)
    {
      switch (AdrMode)
      {
        case ModDir:
          BAsmCode[1] = AdrVals[0];
          BAsmCode[2] = AdrVals[0];
          break;
        case ModIX:
          BAsmCode[1] = 0x0e;
          BAsmCode[2] = 0x0e;
          break;
        case ModX:
          BAsmCode[1] = 0x0f;
          BAsmCode[2] = 0x0f;
          break;
      }
      CodeLen = 3;
    }
  }
}

static void DecodeALU(Word Index)
{
  ALUOrder *pOrder = ALUOrders + Index;

  if (ChkMinCPU(pOrder->MinCPU))
  {
    DecodeAdr(1, ArgCnt, pOrder->Mask);
    if (AdrMode != ModNone)
    {
      switch (AdrMode)
      {
        case ModImm:
          BAsmCode[0] = 0xa0 | pOrder->Code;
          memcpy(BAsmCode + 1, AdrVals, AdrCnt);
          CodeLen = 1 + AdrCnt;
          break;
        case ModDir:
          BAsmCode[0] = 0xb0 | pOrder->Code;
          memcpy(BAsmCode + 1, AdrVals, AdrCnt);
          CodeLen = 1 + AdrCnt;
          break;
        case ModIX:
          BAsmCode[0] = 0xb0 | pOrder->Code;
          BAsmCode[1] = 0x0e;
          CodeLen = 2;
          break;
        case ModX:
          BAsmCode[0] = 0xb0 | pOrder->Code;
          BAsmCode[1] = 0x0f;
          CodeLen = 2;
          break;
	case ModExt:
          BAsmCode[0] = pOrder->Code;
          memcpy(BAsmCode + 1, AdrVals, AdrCnt);
          CodeLen = 1 + AdrCnt;
      }
    }
  }
}

static void DecodeRMW(Word Index)
{
  RMWOrder *pOrder = RMWOrders + Index;

  if (ChkMinCPU(pOrder->MinCPU))
  {
    DecodeAdr(1, ArgCnt, pOrder->Mask);
    if (AdrMode != ModNone)
    {
      switch (AdrMode)
      {
        case ModImm :
          BAsmCode[0] = 0xa0 | pOrder->Code;
          memcpy(BAsmCode + 1, AdrVals, AdrCnt);
          CodeLen = 1 + AdrCnt;
          break;
        case ModDir :
          BAsmCode[0] = 0xb0 ^ pOrder->Code;
          memcpy(BAsmCode + 1, AdrVals, AdrCnt);
          CodeLen = 1+ AdrCnt;
          break;
        case ModTny :
          BAsmCode[0] = AdrVals[0] | pOrder->DCode;
          CodeLen = 1;
          break;
        case ModSrt :
          BAsmCode[0] = AdrVals[0] | pOrder->DCode;
          CodeLen = 1;
          break;
        case ModIX  :
          BAsmCode[0] = 0x0e | pOrder->DCode;
          CodeLen = 1;
          break;
        case ModX :
          BAsmCode[0] = 0x0f | pOrder->DCode;
          CodeLen = 1;
          break;
      }
    }
  }
}

static void DecodeBx(Word Index)
{
  Boolean OK = True;

  if (ChkArgCnt(2, 2))
  {
    if (!strcasecmp(ArgStr[2], "D[X]")) BAsmCode[1] = 0x0e;
    else if  (!strcasecmp(ArgStr[2], "X")) BAsmCode[1] = 0x0f;  
    else BAsmCode[1] = EvalIntExpression(ArgStr[2], Int8, &OK);

    if (OK)
    {
      BAsmCode[0] = EvalIntExpression(ArgStr[1], UInt3, &OK);
      if (OK)
      {
        CodeLen = 2;
        BAsmCode[0] = 0x10 | (BAsmCode[0] << 1) | Index;
      }
    }
  }
}

static void DecodeBRx(Word Index)
{
  Boolean OK = True;
  LongInt AdrInt;

  if (ChkArgCnt(3, 3))
  {
    if (!strcasecmp(ArgStr[2], "D[X]"))
      BAsmCode[1] = 0x0e;
    else if (!strcasecmp(ArgStr[2], "X"))
      BAsmCode[1] = 0x0f;  
    else
      BAsmCode[1] = EvalIntExpression(ArgStr[2], Int8, &OK);

    if (OK)
    {
      BAsmCode[0] = EvalIntExpression(ArgStr[1], UInt3, &OK);
      if (OK)
      {
        AdrInt = EvalIntExpression(ArgStr[3], AdrIntType, &OK) - (EProgCounter() + 3);
        if (OK)
        {
          if ((!SymbolQuestionable) && ((AdrInt < -128) || (AdrInt > 127))) WrError(1370);
          else
          {
            CodeLen = 3;
            BAsmCode[0] = (BAsmCode[0] << 1) | Index;
            BAsmCode[2] = Lo(AdrInt);
          }
        }
      }
    }
  }
}

/*--------------------------------------------------------------------------*/
/* dynamic code table handling */

static void AddFixed(char *NName, CPUVar NMin, Byte NCode)
{
  if (InstrZ >= FixedOrderCnt) exit(255);
  FixedOrders[InstrZ].MinCPU = NMin;
  FixedOrders[InstrZ].Code = NCode;
  AddInstTable(InstTable, NName, InstrZ++, DecodeFixed);
}

static void AddRel(char *NName, CPUVar NMin, Byte NCode)
{
  if (InstrZ >= RelOrderCnt) exit(255);
  RelOrders[InstrZ].MinCPU = NMin;
  RelOrders[InstrZ].Code = NCode;
  AddInstTable(InstTable, NName, InstrZ++, DecodeRel);
}

static void AddALU(char *NName, CPUVar NMin, Byte NCode, Word NMask)
{
  if (InstrZ >= ALUOrderCnt) exit(255);
  ALUOrders[InstrZ].MinCPU = NMin;
  ALUOrders[InstrZ].Code = NCode;
  ALUOrders[InstrZ].Mask = NMask;
  AddInstTable(InstTable, NName, InstrZ++, DecodeALU);
}

static void AddRMW(char *NName, CPUVar NMin, Byte NCode, Byte DCode, Word NMask)
{
  if (InstrZ >= RMWOrderCnt) exit(255);
  RMWOrders[InstrZ].MinCPU = NMin;
  RMWOrders[InstrZ].Code = NCode;
  RMWOrders[InstrZ].DCode = DCode;
  RMWOrders[InstrZ].Mask = NMask;
  AddInstTable(InstTable, NName, InstrZ++, DecodeRMW);
}

static void InitFields(void)
{
  InstTable = CreateInstTable(63);

  FixedOrders = (BaseOrder *) malloc(sizeof(BaseOrder) * FixedOrderCnt); InstrZ = 0;
  AddFixed("SHA" , CPU68RS08, 0x45); AddFixed("SLA" , CPU68RS08, 0x42);
  AddFixed("RTS" , CPU68RS08, 0xbe); AddFixed("TAX" , CPU68RS08, 0xef);
  AddFixed("CLC" , CPU68RS08, 0x38); AddFixed("SEC" , CPU68RS08, 0x39);
  AddFixed("NOP" , CPU68RS08, 0xac); AddFixed("TXA" , CPU68RS08, 0xcf);
  AddFixed("COMA", CPU68RS08, 0x43); AddFixed("LSRA", CPU68RS08, 0x44);
  AddFixed("RORA", CPU68RS08, 0x46); AddFixed("ASLA", CPU68RS08, 0x48);
  AddFixed("LSLA", CPU68RS08, 0x48); AddFixed("ROLA", CPU68RS08, 0x49);
  AddFixed("DECA", CPU68RS08, 0x4a); AddFixed("DECX", CPU68RS08, 0x5f);
  AddFixed("INCA", CPU68RS08, 0x4c); AddFixed("INCX", CPU68RS08, 0x2f);
  AddFixed("CLRA", CPU68RS08, 0x4f); AddFixed("CLRX", CPU68RS08, 0x8f); 
  AddFixed("STOP", CPU68RS08, 0xae); AddFixed("WAIT", CPU68RS08, 0xaf);
  AddFixed("BGND", CPU68RS08, 0xbf);

  RelOrders = (BaseOrder *) malloc(sizeof(BaseOrder) * RelOrderCnt); InstrZ = 0;
  AddRel("BRA" , CPU68RS08, 0x30); AddRel("BRN" , CPU68RS08, 0x00);
  AddRel("BCC" , CPU68RS08, 0x34); AddRel("BCS" , CPU68RS08, 0x35);
  AddRel("BHS" , CPU68RS08, 0x34); AddRel("BLO" , CPU68RS08, 0x35);
  AddRel("BNE" , CPU68RS08, 0x36); AddRel("BEQ" , CPU68RS08, 0x37);
  AddRel("BSR" , CPU68RS08, 0xad); 

  ALUOrders=(ALUOrder *) malloc(sizeof(ALUOrder) * ALUOrderCnt); InstrZ = 0;
  AddALU("ADC" , CPU68RS08, 0x09, MModImm | MModDir | MModIX  | MModX);
  AddALU("AND" , CPU68RS08, 0x04, MModImm | MModDir | MModIX  | MModX);
  AddALU("CMP" , CPU68RS08, 0x01, MModImm | MModDir | MModIX  | MModX);
  AddALU("EOR" , CPU68RS08, 0x08, MModImm | MModDir | MModIX  | MModX);
  AddALU("ORA" , CPU68RS08, 0x0a, MModImm | MModDir | MModIX  | MModX);
  AddALU("SBC" , CPU68RS08, 0x02, MModImm | MModDir | MModIX  | MModX);
  AddALU("JMP" , CPU68RS08, 0xbc, MModExt);
  AddALU("JSR" , CPU68RS08, 0xbd, MModExt);

  RMWOrders = (RMWOrder *) malloc(sizeof(RMWOrder) * RMWOrderCnt); InstrZ = 0;
  AddRMW("ADD" , CPU68RS08, 0x0b, 0x60, MModImm | MModDir | MModTny           | MModIX | MModX );
  AddRMW("SUB" , CPU68RS08, 0x00, 0x70, MModImm | MModDir | MModTny           | MModIX | MModX );
  AddRMW("DEC" , CPU68RS08, 0x8a, 0x50,           MModDir | MModTny           | MModIX | MModX );
  AddRMW("INC" , CPU68RS08, 0x8c, 0x20,           MModDir | MModTny           | MModIX | MModX );
  AddRMW("CLR" , CPU68RS08, 0x8f, 0x80,           MModDir           | MModSrt | MModIX | MModX );
  AddRMW("LDA" , CPU68RS08, 0x06, 0xc0, MModImm | MModDir           | MModSrt | MModIX | MModX );
  AddRMW("STA" , CPU68RS08, 0x07, 0xe0,           MModDir           | MModSrt | MModIX | MModX );


  AddInstTable(InstTable, "CBEQA", 1, DecodeCBEQx);

  AddInstTable(InstTable, "CBEQ" , 0, DecodeCBEQ);

  AddInstTable(InstTable, "DBNZA", 1, DecodeDBNZx);
  AddInstTable(InstTable, "DBNZX", 0, DecodeDBNZx);

  AddInstTable(InstTable, "DBNZ" , 0, DecodeDBNZ);

  AddInstTable(InstTable, "MOV"  , 0, DecodeMOV);

  AddInstTable(InstTable, "TST"  , 0, DecodeTST);
  AddInstTable(InstTable, "TSTX" , 1, DecodeTST);
  AddInstTable(InstTable, "TSTA" , 2, DecodeTST);  

  AddInstTable(InstTable, "LDX"  , 0, DecodeLDX);
  AddInstTable(InstTable, "STX"  , 1, DecodeLDX);

  AddInstTable(InstTable, "BCLR" , 0x01, DecodeBx);
  AddInstTable(InstTable, "BSET" , 0x00, DecodeBx);
  AddInstTable(InstTable, "BRCLR", 0x01, DecodeBRx);
  AddInstTable(InstTable, "BRSET", 0x00, DecodeBRx);
}

static void DeinitFields(void)
{
  DestroyInstTable(InstTable);
  free(FixedOrders);
  free(RelOrders);
  free(ALUOrders);
  free(RMWOrders);
}

/*--------------------------------------------------------------------------*/
/* main functions */

static void MakeCode_68rs08(void)
{
  int l;
  char ch;

  CodeLen = 0; DontPrint = False; OpSize = eSymbolSizeUnknown;

  /* deduce operand size No size is zero-length string -> '\0' */

  if (!DecodeMoto16AttrSize(*AttrPart, &OpSize, False))
    return;

  /* zu ignorierendes */

  if (Memo(""))
    return;

  /* Pseudoanweisungen */

  if (DecodeMotoPseudo(True))
    return;
  if (DecodeMoto16Pseudo(OpSize, True))
    return;

  l = strlen(OpPart.Str);
  ch = OpPart.Str[l - 1];
  if ((ch >= '0') && (ch <= '7'))
  {
    int z;

    for (z = ArgCnt; z >= 1; z--) strcpy(ArgStr[z + 1], ArgStr[z]);
    *ArgStr[1] = ch; ArgStr[1][1] = '\0'; ArgCnt++;
    OpPart.Str[l - 1] = '\0';
  }

  if (!LookupInstTable(InstTable, OpPart.Str))
    WrStrErrorPos(ErrNum_UnknownOpcode, &OpPart);
}

static Boolean IsDef_68rs08(void)
{
  return False;
}

static void SwitchFrom_68rs08(void)
{
  DeinitFields();
}

static void SwitchTo_68rs08(void)
{
  TurnWords = False;
  ConstMode = ConstModeMoto;
  SetIsOccupied = False;

  PCSymbol = "*";
  HeaderID = 0x5e;
  NOPCode = 0xac;
  DivideChars = ",";
  HasAttrs = True;
  AttrChars = ".";

  ValidSegs = (1 << SegCode);
  Grans[SegCode] = 1; ListGrans[SegCode] = 1; SegInits[SegCode] = 0;
  SegLimits[SegCode] = 0x3fff;
  AdrIntType = UInt14;

  MakeCode = MakeCode_68rs08;
  IsDef = IsDef_68rs08;
  SwitchFrom = SwitchFrom_68rs08;
  InitFields();
  AddMoto16PseudoONOFF();
}

void code68rs08_init(void)
{
  CPU68RS08 = AddCPU("68RS08", SwitchTo_68rs08);
  AddCopyright("68RS08-Generator (C) 2006 Andreas Bolsch");
}
