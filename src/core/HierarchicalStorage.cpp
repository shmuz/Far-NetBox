//------------------------------------------------------------------------------
#include <vcl.h>
#pragma hdrstop

#define TRACE_READING NOTRACING
#define TRACE_WRITING NOTRACING
#define TRACE_ACCESS NOTRACING

#include "Common.h"
#include "Exceptions.h"
#include "PuttyIntf.h"
#include "HierarchicalStorage.h"
#include <TextsCore.h>
//------------------------------------------------------------------------------
#pragma package(smart_init)
//------------------------------------------------------------------------------
#define READ_REGISTRY(Method) \
  if (FRegistry->ValueExists(Name)) \
  try { return FRegistry->Method(Name); } catch(...) { FFailed++; return Default; } \
  else return Default;
#define WRITE_REGISTRY(Method) \
  try { FRegistry->Method(Name, Value); } catch(...) { FFailed++; }
//------------------------------------------------------------------------------
UnicodeString MungeStr(const UnicodeString & Str, bool ForceAnsi)
{
  RawByteString Source;
  if (ForceAnsi)
  {
    Source = AnsiString(W2MB(Str.c_str()).c_str());
  }
  else
  {
    Source = UTF8String(Str);
    if (Source.Length() > Str.Length())
    {
      Source.Insert(Bom.c_str(), 1);
    }
  }
  // should contain ASCII characters only
  RawByteString Dest;
  Dest.SetLength(Source.Length() * 3 + 1);
  putty_mungestr(Source.c_str(), const_cast<char *>(Dest.c_str()));
  PackStr(Dest);
  return UnicodeString(Dest.c_str(), Dest.Length());
}
//------------------------------------------------------------------------------
UnicodeString UnMungeStr(const UnicodeString & Str)
{
  // Str should contain ASCII characters only
  RawByteString Source = Str;
  RawByteString Dest;
  Dest.SetLength(Source.Length() + 1);
  putty_unmungestr(Source.c_str(), const_cast<char *>(Dest.c_str()), static_cast<int>(Dest.Length()));
  UnicodeString Result;
  if (Dest.Pos(Bom.c_str()) == 1)
  {
    Dest.Delete(1, Bom.size());
    Result = UTF8String(Dest.c_str());
  }
  else
  {
    Result = AnsiString(MB2W(Dest.c_str()).c_str());
  }
  return Result;
}
//------------------------------------------------------------------------------
UnicodeString PuttyMungeStr(const UnicodeString & Str)
{
  return MungeStr(Str, false);
}
//------------------------------------------------------------------------------
UnicodeString PuttyUnMungeStr(const UnicodeString & Str)
{
  return UnMungeStr(Str);
}
//------------------------------------------------------------------------------
UnicodeString MungeIniName(const UnicodeString & Str)
{
  intptr_t P = Str.Pos(L"=");
  // make this fast for now
  if (P > 0)
  {
    return StringReplace(Str, L"=", L"%3D", TReplaceFlags() << rfReplaceAll);
  }
  else
  {
    return Str;
  }
}
//------------------------------------------------------------------------------
UnicodeString UnMungeIniName(const UnicodeString & Str)
{
  intptr_t P = Str.Pos(L"%3D");
  // make this fast for now
  if (P > 0)
  {
    return StringReplace(Str, L"%3D", L"=", TReplaceFlags() << rfReplaceAll);
  }
  else
  {
    return Str;
  }
}
//===========================================================================
THierarchicalStorage::THierarchicalStorage(const UnicodeString & AStorage)
{
  CALLSTACK;
  FStorage = AStorage;
  FKeyHistory = new TStringList();
  SetAccessMode(smRead);
  SetExplicit(false);
  SetForceAnsi(true);
  SetMungeStringValues(true);
}
//------------------------------------------------------------------------------
THierarchicalStorage::~THierarchicalStorage()
{
  delete FKeyHistory;
}
//------------------------------------------------------------------------------
void THierarchicalStorage::Flush()
{
}
//------------------------------------------------------------------------------
void THierarchicalStorage::SetAccessMode(TStorageAccessMode Value)
{
  FAccessMode = Value;
}
//------------------------------------------------------------------------------
UnicodeString THierarchicalStorage::GetCurrentSubKeyMunged() const
{
  if (FKeyHistory->GetCount())
  {
    return FKeyHistory->Strings[FKeyHistory->GetCount() - 1];
  }
  else
  {
    return L"";
  }
}
//------------------------------------------------------------------------------
UnicodeString THierarchicalStorage::GetCurrentSubKey() const
{
  return UnMungeStr(GetCurrentSubKeyMunged());
}
//------------------------------------------------------------------------------
bool THierarchicalStorage::OpenRootKey(bool CanCreate)
{
  return OpenSubKey(L"", CanCreate);
}
//------------------------------------------------------------------------------
UnicodeString THierarchicalStorage::MungeKeyName(const UnicodeString & Key)
{
  UnicodeString Result = MungeStr(Key, GetForceAnsi());
  // if there's already ANSI-munged subkey, keep ANSI munging
  if ((Result != Key) && !GetForceAnsi() && DoKeyExists(Key, true))
  {
    Result = MungeStr(Key, true);
  }
  return Result;
}
//------------------------------------------------------------------------------
bool THierarchicalStorage::OpenSubKey(const UnicodeString & SubKey, bool CanCreate, bool Path)
{
  bool Result;
  UnicodeString MungedKey;
  UnicodeString SubKey2 = SubKey;
  if (Path)
  {
    assert(SubKey2.IsEmpty() || (SubKey2[SubKey2.Length()] != L'\\'));
    Result = true;
    while (!SubKey2.IsEmpty() && Result)
    {
      if (!MungedKey.IsEmpty())
      {
        MungedKey += L'\\';
      }
      MungedKey += MungeKeyName(CutToChar(SubKey2, L'\\', false));
      Result = DoOpenSubKey(MungedKey, CanCreate);
    }

    // hack to restore last opened key for registry storage
    if (!Result)
    {
      FKeyHistory->Add(IncludeTrailingBackslash(GetCurrentSubKey() + MungedKey));
      CloseSubKey();
    }
  }
  else
  {
    MungedKey = MungeKeyName(SubKey2);
    Result = DoOpenSubKey(MungedKey, CanCreate);
  }

  if (Result)
  {
    FKeyHistory->Add(IncludeTrailingBackslash(GetCurrentSubKey() + MungedKey));
  }

  return Result;
}
//------------------------------------------------------------------------------
void THierarchicalStorage::CloseSubKey()
{
  if (FKeyHistory->GetCount() == 0)
    throw Exception(L"");
  else
    FKeyHistory->Delete(FKeyHistory->GetCount() - 1);
}
//------------------------------------------------------------------------------
void THierarchicalStorage::ClearSubKeys()
{
  TStringList * SubKeys = new TStringList();
  TRY_FINALLY (
  {
    GetSubKeyNames(SubKeys);
    for (intptr_t Index = 0; Index < SubKeys->GetCount(); ++Index)
    {
      RecursiveDeleteSubKey(SubKeys->Strings[Index]);
    }
  }
  ,
  {
    delete SubKeys;
  }
  );
}
//------------------------------------------------------------------------------
void THierarchicalStorage::RecursiveDeleteSubKey(const UnicodeString & Key)
{
  if (OpenSubKey(Key, false))
  {
    ClearSubKeys();
    CloseSubKey();
  }
  DeleteSubKey(Key);
}
//------------------------------------------------------------------------------
bool THierarchicalStorage::HasSubKeys()
{
  bool Result;
  TStrings * SubKeys = new TStringList();
  TRY_FINALLY (
  {
    GetSubKeyNames(SubKeys);
    Result = (SubKeys->GetCount() > 0);
  }
  ,
  {
    delete SubKeys;
  }
  );
  return Result;
}
//------------------------------------------------------------------------------
bool THierarchicalStorage::HasSubKey(const UnicodeString & SubKey)
{
  bool Result = OpenSubKey(SubKey, false);
  if (Result)
  {
    CloseSubKey();
  }
  return Result;
}
//------------------------------------------------------------------------------
bool THierarchicalStorage::KeyExists(const UnicodeString & SubKey)
{
  return DoKeyExists(SubKey, GetForceAnsi());
}
//------------------------------------------------------------------------------
void THierarchicalStorage::ReadValues(Classes::TStrings* Strings,
  bool MaintainKeys)
{
  TStrings * Names = new TStringList();
  TRY_FINALLY (
  {
    GetValueNames(Names);
    for (intptr_t Index = 0; Index < Names->GetCount(); ++Index)
    {
      if (MaintainKeys)
      {
        Strings->Add(FORMAT(L"%s=%s", Names->Strings[Index].c_str(),
          ReadString(Names->Strings[Index], L"").c_str()));
      }
      else
      {
        Strings->Add(ReadString(Names->Strings[Index], L""));
      }
    }
  }
  ,
  {
    delete Names;
  }
  );
}
//------------------------------------------------------------------------------
void THierarchicalStorage::ClearValues()
{
  TStrings * Names = new TStringList();
  TRY_FINALLY (
  {
    GetValueNames(Names);
    for (intptr_t Index = 0; Index < Names->GetCount(); ++Index)
    {
      DeleteValue(Names->Strings[Index]);
    }
  }
  ,
  {
    delete Names;
  }
  );
}
//------------------------------------------------------------------------------
void THierarchicalStorage::WriteValues(Classes::TStrings * Strings,
  bool MaintainKeys)
{
  ClearValues();

  if (Strings)
  {
    for (intptr_t Index = 0; Index < Strings->GetCount(); ++Index)
    {
      if (MaintainKeys)
      {
        assert(Strings->Strings[Index].Pos(L"=") > 1);
        WriteString(Strings->Names[Index], Strings->Values[Strings->Names[Index]]);
      }
      else
      {
        WriteString(IntToStr(Index), Strings->Strings[Index]);
      }
    }
  }
}
//------------------------------------------------------------------------------
UnicodeString THierarchicalStorage::ReadString(const UnicodeString & Name, const UnicodeString & Default)
{
  UnicodeString Result;
  if (GetMungeStringValues())
  {
    Result = UnMungeStr(ReadStringRaw(Name, MungeStr(Default, GetForceAnsi())));
  }
  else
  {
    Result = ReadStringRaw(Name, Default);
  }
  return Result;
}
//------------------------------------------------------------------------------
RawByteString THierarchicalStorage::ReadBinaryData(const UnicodeString & Name)
{
  size_t Size = BinaryDataSize(Name);
  RawByteString Value;
  Value.SetLength(Size);
  ReadBinaryData(Name, static_cast<void *>(const_cast<char *>(Value.c_str())), Size);
  return Value;
}
//------------------------------------------------------------------------------
RawByteString THierarchicalStorage::ReadStringAsBinaryData(const UnicodeString & Name, const RawByteString & Default)
{
  UnicodeString UnicodeDefault = UnicodeString(AnsiString(Default.c_str(), Default.Length()));
  // This should be exactly the same operation as calling ReadString in
  // C++Builder 6 (non-Unicode) on Unicode-based OS
  // (conversion is done by Ansi layer of the OS)
  UnicodeString String = ReadString(Name, UnicodeDefault);
  AnsiString Ansi = AnsiString(String);
  RawByteString Result = RawByteString(Ansi.c_str(), Ansi.Length());
  return Result;
}
//------------------------------------------------------------------------------
void THierarchicalStorage::WriteString(const UnicodeString & Name, const UnicodeString & Value)
{
  if (GetMungeStringValues())
  {
    WriteStringRaw(Name, MungeStr(Value, GetForceAnsi()));
  }
  else
  {
    WriteStringRaw(Name, Value);
  }
}
//------------------------------------------------------------------------------
void THierarchicalStorage::WriteBinaryData(const UnicodeString & Name,
  const RawByteString & Value)
{
  WriteBinaryData(Name, Value.c_str(), Value.Length());
}
//------------------------------------------------------------------------------
void THierarchicalStorage::WriteBinaryDataAsString(const UnicodeString & Name, const RawByteString & Value)
{
  // This should be exactly the same operation as calling WriteString in
  // C++Builder 6 (non-Unicode) on Unicode-based OS
  // (conversion is done by Ansi layer of the OS)
  AnsiString Ansi = AnsiString(Value.c_str(), Value.Length());
  WriteString(Name, UnicodeString(Ansi));
}
//------------------------------------------------------------------------------
UnicodeString THierarchicalStorage::IncludeTrailingBackslash(const UnicodeString & S)
{
  // expanded from ?: as it caused memory leaks
  if (S.IsEmpty())
  {
    return S;
  }
  else
  {
    return ::IncludeTrailingBackslash(S);
  }
}
//------------------------------------------------------------------------------
UnicodeString THierarchicalStorage::ExcludeTrailingBackslash(const UnicodeString & S)
{
  // expanded from ?: as it caused memory leaks
  if (S.IsEmpty())
  {
    return S;
  }
  else
  {
    return ::ExcludeTrailingBackslash(S);
  }
}
//===========================================================================
TRegistryStorage::TRegistryStorage(const UnicodeString & AStorage) :
  THierarchicalStorage(IncludeTrailingBackslash(AStorage)),
  FRegistry(NULL)
{
  CALLSTACK;
  CTRACEFMT(TRACE_ACCESS, "1 [%s]", AStorage.c_str());
  Init();
}
//------------------------------------------------------------------------------
TRegistryStorage::TRegistryStorage(const UnicodeString & AStorage, HKEY ARootKey):
  THierarchicalStorage(IncludeTrailingBackslash(AStorage)),
  FRegistry(NULL)
{
  Init();
  CTRACEFMT(TRACE_ACCESS, "1 [%s] [%x]", AStorage.c_str(), int(ARootKey));
  FRegistry->RootKey = ARootKey;
}
//------------------------------------------------------------------------------
void TRegistryStorage::Init()
{
  CCALLSTACK(TRACE_ACCESS);
  FFailed = 0;
  FRegistry = new TRegistry();
  FRegistry->Access = KEY_READ;
}
//------------------------------------------------------------------------------
TRegistryStorage::~TRegistryStorage()
{
  CCALLSTACK(TRACE_ACCESS);
  delete FRegistry;
};
//------------------------------------------------------------------------------
bool TRegistryStorage::Copy(TRegistryStorage * Storage)
{
  CCALLSTACK(TRACE_ACCESS);
  TRegistry * Registry = Storage->FRegistry;
  bool Result = true;
  TStrings * Names = new TStringList();
  std::vector<unsigned char, custom_nballocator_t<unsigned char> > Buffer(1024, 0);
  TRY_FINALLY (
  {
    Registry->GetValueNames(Names);
    intptr_t Index = 0;
    while ((Index < Names->GetCount()) && Result)
    {
      UnicodeString Name = MungeStr(Names->Strings[Index], GetForceAnsi());
      DWORD Size = static_cast<DWORD>(Buffer.size());
      DWORD Type;
      int RegResult = 0;
      do
      {
        RegResult = RegQueryValueEx(Registry->CurrentKey.get(), Name.c_str(), NULL,
          &Type, &Buffer[0], &Size);
        if (RegResult == ERROR_MORE_DATA)
        {
          Buffer.resize(Size);
        }
      } while (RegResult == ERROR_MORE_DATA);

      Result = (RegResult == ERROR_SUCCESS);
      if (Result)
      {
        RegResult = RegSetValueEx(FRegistry->CurrentKey.get(), Name.c_str(), NULL, Type,
          &Buffer[0], Size);
        Result = (RegResult == ERROR_SUCCESS);
      }

      ++Index;
    }
  }
  ,
  {
    delete Names;
  }
  );
  return Result;
}
//------------------------------------------------------------------------------
UnicodeString TRegistryStorage::GetSource()
{
  return RootKeyToStr(FRegistry->RootKey.get()) + L"\\" + GetStorage();
}
//------------------------------------------------------------------------------
void TRegistryStorage::SetAccessMode(TStorageAccessMode value)
{
  CCALLSTACK(TRACE_ACCESS);
  THierarchicalStorage::SetAccessMode(value);
  if (FRegistry)
  {
    switch (GetAccessMode()) {
      case smRead:
        FRegistry->Access = KEY_READ;
        break;

      case smReadWrite:
      default:
        FRegistry->Access = KEY_READ | KEY_WRITE;
        break;
    }
  }
}
//------------------------------------------------------------------------------
bool TRegistryStorage::DoOpenSubKey(const UnicodeString & SubKey, bool CanCreate)
{
  CCALLSTACK(TRACE_ACCESS);
  if (FKeyHistory->GetCount() > 0) { FRegistry->CloseKey(); }
  UnicodeString K = ExcludeTrailingBackslash(GetStorage() + GetCurrentSubKey() + SubKey);
  CTRACEFMT(TRACE_ACCESS, "1 [%s] [%d]", K.c_str(), int(CanCreate));
  return FRegistry->OpenKey(K, CanCreate);
}
//------------------------------------------------------------------------------
void TRegistryStorage::CloseSubKey()
{
  CCALLSTACK(TRACE_ACCESS);
  FRegistry->CloseKey();
  THierarchicalStorage::CloseSubKey();
  if (FKeyHistory->GetCount())
  {
    FRegistry->OpenKey(GetStorage() + GetCurrentSubKeyMunged(), True);
  }
}
//------------------------------------------------------------------------------
bool TRegistryStorage::DeleteSubKey(const UnicodeString & SubKey)
{
  CCALLSTACK(TRACE_ACCESS);
  UnicodeString K;
  if (FKeyHistory->GetCount() == 0) { K = GetStorage() + GetCurrentSubKey(); }
  K += MungeKeyName(SubKey);
  CTRACEFMT(TRACE_ACCESS, "1 [%s]", K.c_str());
  return FRegistry->DeleteKey(K);
}
//------------------------------------------------------------------------------
void TRegistryStorage::GetSubKeyNames(Classes::TStrings* Strings)
{
  CCALLSTACK(TRACE_ACCESS);
  FRegistry->GetKeyNames(Strings);
  for (intptr_t Index = 0; Index < Strings->GetCount(); ++Index)
  {
    Strings->Strings[Index] = UnMungeStr(Strings->Strings[Index]);
    CTRACEFMT(TRACE_ACCESS, "1 [%s]", Strings->Strings[Index].c_str());
  }
}
//------------------------------------------------------------------------------
void TRegistryStorage::GetValueNames(Classes::TStrings* Strings)
{
  CCALLSTACK(TRACE_ACCESS);
  FRegistry->GetValueNames(Strings);
  CTRACEFMT(TRACE_ACCESS, "[%s]", Strings->CommaText.c_str());
}
//------------------------------------------------------------------------------
bool TRegistryStorage::DeleteValue(const UnicodeString & Name)
{
  CCALLSTACK(TRACE_ACCESS);
  CTRACEFMT(TRACE_ACCESS, "[%s]", Name.c_str());
  return FRegistry->DeleteValue(Name);
}
//------------------------------------------------------------------------------
bool TRegistryStorage::DoKeyExists(const UnicodeString & SubKey, bool AForceAnsi)
{
  CCALLSTACK(TRACE_ACCESS);
  UnicodeString K = MungeStr(SubKey, AForceAnsi);
  bool Result = FRegistry->KeyExists(K);
  CTRACEFMT(TRACE_ACCESS, "[%s] [%d]", K.c_str(), int(Result));
  return Result;
}
//------------------------------------------------------------------------------
bool TRegistryStorage::ValueExists(const UnicodeString & Value)
{
  CCALLSTACK(TRACE_ACCESS);
  bool Result = FRegistry->ValueExists(Value);
  CTRACEFMT(TRACE_ACCESS, "[%s] [%d]", Value.c_str(), int(Result));
  return Result;
}
//------------------------------------------------------------------------------
size_t TRegistryStorage::BinaryDataSize(const UnicodeString & Name)
{
  CCALLSTACK(TRACE_ACCESS);
  size_t Result = FRegistry->GetDataSize(Name);
  CTRACEFMT(TRACE_ACCESS, "[%s] [%d]", Name.c_str(), int(Result));
  return Result;
}
//------------------------------------------------------------------------------
bool TRegistryStorage::ReadBool(const UnicodeString & Name, bool Default)
{
  CCALLSTACK(TRACE_READING);
  CTRACEFMT(TRACE_READING, "%s", Name.c_str());
  READ_REGISTRY(ReadBool);
}
//------------------------------------------------------------------------------
TDateTime TRegistryStorage::ReadDateTime(const UnicodeString & Name, TDateTime Default)
{
  CCALLSTACK(TRACE_READING);
  CTRACEFMT(TRACE_READING, "%s", Name.c_str());
  READ_REGISTRY(ReadDateTime);
}
//------------------------------------------------------------------------------
double TRegistryStorage::ReadFloat(const UnicodeString & Name, double Default)
{
  CCALLSTACK(TRACE_READING);
  CTRACEFMT(TRACE_READING, "%s", Name.c_str());
  READ_REGISTRY(ReadFloat);
}
//------------------------------------------------------------------------------
intptr_t TRegistryStorage::ReadInteger(const UnicodeString & Name, intptr_t Default)
{
  CCALLSTACK(TRACE_READING);
  CTRACEFMT(TRACE_READING, "%s", Name.c_str());
  READ_REGISTRY(ReadInteger);
}
//------------------------------------------------------------------------------
__int64 TRegistryStorage::ReadInt64(const UnicodeString & Name, __int64 Default)
{
  CCALLSTACK(TRACE_READING);
  CTRACEFMT(TRACE_READING, "%s", Name.c_str());
  __int64 Result = Default;
  if (FRegistry->ValueExists(Name))
  {
    try
    {
      FRegistry->ReadBinaryData(Name, &Result, sizeof(Result));
    }
    catch(...)
    {
      FFailed++;
    }
  }
  return Result;
}
//------------------------------------------------------------------------------
UnicodeString TRegistryStorage::ReadStringRaw(const UnicodeString & Name, const UnicodeString & Default)
{
  CCALLSTACK(TRACE_READING);
  CTRACEFMT(TRACE_READING, "%s", Name.c_str());
  READ_REGISTRY(ReadString);
}
//------------------------------------------------------------------------------
size_t TRegistryStorage::ReadBinaryData(const UnicodeString & Name,
  void * Buffer, size_t Size)
{
  CCALLSTACK(TRACE_READING);
  CTRACEFMT(TRACE_READING, "%s", Name.c_str());
  size_t Result;
  if (FRegistry->ValueExists(Name))
  {
    try
    {
      Result = FRegistry->ReadBinaryData(Name, Buffer, Size);
    }
    catch(...)
    {
      Result = 0;
      FFailed++;
    }
  }
  else
  {
    Result = 0;
  }
  return Result;
}
//------------------------------------------------------------------------------
void TRegistryStorage::WriteBool(const UnicodeString & Name, bool Value)
{
  CCALLSTACK(TRACE_WRITING);
  CTRACEFMT(TRACE_WRITING, "[%s] [%s] [%d]", FRegistry->CurrentPath.c_str(), Name.c_str(), int(Value));
  WRITE_REGISTRY(WriteBool);
}
//------------------------------------------------------------------------------
void TRegistryStorage::WriteDateTime(const UnicodeString & Name, TDateTime Value)
{
  WRITE_REGISTRY(WriteDateTime);
}
//------------------------------------------------------------------------------
void TRegistryStorage::WriteFloat(const UnicodeString & Name, double Value)
{
  CCALLSTACK(TRACE_WRITING);
  CTRACEFMT(TRACE_WRITING, "[%s] [%s] [%d]", FRegistry->CurrentPath.c_str(), Name.c_str(), int(Value * 1000));
  WRITE_REGISTRY(WriteFloat);
}
//------------------------------------------------------------------------------
void TRegistryStorage::WriteStringRaw(const UnicodeString & Name, const UnicodeString & Value)
{
  CCALLSTACK(TRACE_WRITING);
  CTRACEFMT(TRACE_WRITING, "[%s] [%s] [%s]", FRegistry->CurrentPath.c_str(), Name.c_str(), Value);
  WRITE_REGISTRY(WriteString);
}
//------------------------------------------------------------------------------
void TRegistryStorage::WriteInteger(const UnicodeString & Name, intptr_t Value)
{
  CCALLSTACK(TRACE_WRITING);
  CTRACEFMT(TRACE_WRITING, "[%s] [%s] [%d]", FRegistry->CurrentPath.c_str(), Name.c_str(), Value);
  WRITE_REGISTRY(WriteInteger);
}
//------------------------------------------------------------------------------
void TRegistryStorage::WriteInt64(const UnicodeString & Name, __int64 Value)
{
  CCALLSTACK(TRACE_WRITING);
  try
  {
    CTRACEFMT(TRACE_WRITING, "[%s] [%s] [%d]", FRegistry->CurrentPath.c_str(), Name.c_str(), int(Value));
    FRegistry->WriteBinaryData(Name, &Value, sizeof(Value));
  }
  catch(...)
  {
    FFailed++;
  }
}
//------------------------------------------------------------------------------
void TRegistryStorage::WriteBinaryData(const UnicodeString & Name,
  const void * Buffer, size_t Size)
{
  try
  {
    FRegistry->WriteBinaryData(Name, const_cast<void *>(Buffer), Size);
  }
  catch(...)
  {
    FFailed++;
  }
}
//------------------------------------------------------------------------------
intptr_t TRegistryStorage::GetFailed()
{
  intptr_t Result = FFailed;
  FFailed = 0;
  return Result;
}
#ifndef _MSC_VER
//===========================================================================
TCustomIniFileStorage::TCustomIniFileStorage(const UnicodeString & Storage, TCustomIniFile * IniFile) :
  THierarchicalStorage(Storage),
  FIniFile(IniFile)
{
}
//------------------------------------------------------------------------------
TCustomIniFileStorage::~TCustomIniFileStorage()
{
  delete FIniFile;
}
//------------------------------------------------------------------------------
UnicodeString TCustomIniFileStorage::GetSource()
{
  return GetStorage();
}
//------------------------------------------------------------------------------
UnicodeString TCustomIniFileStorage::GetCurrentSection() const
{
  return ExcludeTrailingBackslash(GetCurrentSubKeyMunged());
}
//------------------------------------------------------------------------------
bool TCustomIniFileStorage::DoOpenSubKey(const UnicodeString & SubKey, bool CanCreate)
{
  bool Result = CanCreate;

  if (!Result)
  {
    TStringList * Sections = new TStringList();
    TRY_FINALLY (
    {
      Sections->Sorted = true;
      FIniFile->ReadSections(Sections);
      UnicodeString NewKey = ExcludeTrailingBackslash(GetCurrentSubKey()+SubKey);
      if (Sections->GetCount())
      {
        int Index = -1;
        Result = Sections->Find(NewKey, Index);
        if (!Result && Index < Sections->GetCount() &&
            Sections->Strings[Index].SubString(1, NewKey.Length()+1) == NewKey + L"\\")
        {
          Result = true;
        }
      }
    }
    ,
    {
      delete Sections;
    }
    );
  }

  return Result;
}
//------------------------------------------------------------------------------
bool TCustomIniFileStorage::DeleteSubKey(const UnicodeString & SubKey)
{
  bool Result;
  try
  {
    FIniFile->EraseSection(GetCurrentSubKey() + MungeKeyName(SubKey));
    Result = true;
  }
  catch (...)
  {
    Result = false;
  }
  return Result;
}
//------------------------------------------------------------------------------
void TCustomIniFileStorage::GetSubKeyNames(Classes::TStrings* Strings)
{
  TStrings * Sections = new TStringList();
  TRY_FINALLY (
  {
    Strings->Clear();
    FIniFile->ReadSections(Sections);
    for (intptr_t i = 0; i < Sections->GetCount(); i++)
    {
      UnicodeString Section = Sections->Strings[i];
      if (AnsiCompareText(GetCurrentSubKey(),
          Section.SubString(1, GetCurrentSubKey().Length())) == 0)
      {
        UnicodeString SubSection = Section.SubString(GetCurrentSubKey().Length() + 1,
          Section.Length() - GetCurrentSubKey().Length());
        intptr_t P = SubSection.Pos(L"\\");
        if (P)
        {
          SubSection.SetLength(P - 1);
        }
        if (Strings->IndexOf(SubSection) < 0)
        {
          Strings->Add(UnMungeStr(SubSection));
        }
      }
    }
  }
  ,
  {
    delete Sections;
  }
  );
}
//------------------------------------------------------------------------------
void TCustomIniFileStorage::GetValueNames(Classes::TStrings* Strings)
{
  FIniFile->ReadSection(GetCurrentSection(), Strings);
  for (intptr_t Index = 0; Index < Strings->GetCount(); ++Index)
  {
    Strings->Strings[Index] = UnMungeIniName(Strings->Strings[Index]);
  }
}
//------------------------------------------------------------------------------
bool TCustomIniFileStorage::DoKeyExists(const UnicodeString & SubKey, bool AForceAnsi)
{
  return FIniFile->SectionExists(GetCurrentSubKey() + MungeStr(SubKey, AForceAnsi));
}
//------------------------------------------------------------------------------
bool TCustomIniFileStorage::ValueExists(const UnicodeString & Value)
{
  return FIniFile->ValueExists(GetCurrentSection(), MungeIniName(Value));
}
//------------------------------------------------------------------------------
bool TCustomIniFileStorage::DeleteValue(const UnicodeString & Name)
{
  FIniFile->DeleteKey(GetCurrentSection(), MungeIniName(Name));
  return true;
}
//------------------------------------------------------------------------------
size_t TCustomIniFileStorage::BinaryDataSize(const UnicodeString & Name)
{
  return ReadStringRaw(Name, L"").Length() / 2;
}
//------------------------------------------------------------------------------
bool TCustomIniFileStorage::ReadBool(const UnicodeString & Name, bool Default)
{
  CCALLSTACK(TRACE_READING);
  CTRACEFMT(TRACE_READING, "[%s] [%s]", GetCurrentSection().c_str(), Name.c_str());
  return FIniFile->ReadBool(GetCurrentSection(), MungeIniName(Name), Default);
}
//------------------------------------------------------------------------------
intptr_t TCustomIniFileStorage::ReadInteger(const UnicodeString & Name, intptr_t Default)
{
  CCALLSTACK(TRACE_READING);
  intptr_t Result = FIniFile->ReadInteger(GetCurrentSection(), MungeIniName(Name), Default);
  CTRACEFMT(TRACE_READING, "[%s] [%s] [%d]", GetCurrentSection().c_str(), Name.c_str(), Result);
  return Result;
}
//------------------------------------------------------------------------------
__int64 TCustomIniFileStorage::ReadInt64(const UnicodeString & Name, __int64 Default)
{
  CCALLSTACK(TRACE_READING);
  CTRACEFMT(TRACE_READING, "[%s] [%s]", GetCurrentSection().c_str(), Name.c_str());
  __int64 Result = Default;
  UnicodeString Str;
  Str = ReadStringRaw(Name, L"");
  if (!Str.IsEmpty())
  {
    Result = StrToInt64Def(Str, Default);
  }
  return Result;
}
//------------------------------------------------------------------------------
TDateTime TCustomIniFileStorage::ReadDateTime(const UnicodeString & Name, TDateTime Default)
{
  CCALLSTACK(TRACE_READING);
  CTRACEFMT(TRACE_READING, "[%s] [%s]", GetCurrentSection().c_str(), Name.c_str());
  TDateTime Result;
  UnicodeString Value = FIniFile->ReadString(GetCurrentSection(), MungeIniName(Name), L"");
  if (Value.IsEmpty())
  {
    Result = Default;
  }
  else
  {
    try
    {
      RawByteString Raw = HexToBytes(Value);
      if (static_cast<size_t>(Raw.Length()) == sizeof(Result))
      {
        memcpy(&Result, Raw.c_str(), sizeof(Result));
      }
      else
      {
        Result = StrToDateTime(Value);
      }
    }
    catch(...)
    {
      Result = Default;
    }
  }

  return Result;
}
//------------------------------------------------------------------------------
double TCustomIniFileStorage::ReadFloat(const UnicodeString & Name, double Default)
{
  CCALLSTACK(TRACE_READING);
  CTRACEFMT(TRACE_READING, "[%s] [%s]", GetCurrentSection().c_str(), Name.c_str());
  double Result;
  UnicodeString Value = FIniFile->ReadString(GetCurrentSection(), MungeIniName(Name), L"");
  if (Value.IsEmpty())
  {
    Result = Default;
  }
  else
  {
    try
    {
      RawByteString Raw = HexToBytes(Value);
      if (static_cast<size_t>(Raw.Length()) == sizeof(Result))
      {
        memcpy(&Result, Raw.c_str(), sizeof(Result));
      }
      else
      {
        Result = static_cast<double>(StrToFloat(Value));
      }
    }
    catch(...)
    {
      Result = Default;
    }
  }

  return Result;
}
//------------------------------------------------------------------------------
UnicodeString TCustomIniFileStorage::ReadStringRaw(const UnicodeString & Name, UnicodeString & Default)
{
  CCALLSTACK(TRACE_READING);
  AnsiString Result = FIniFile->ReadString(GetCurrentSection(), MungeIniName(Name), Default);
  CTRACEFMT(TRACE_READING, "[%s] [%s] [%s]", GetCurrentSection().c_str(), Name.c_str(), Result.c_str());
  return Result;
}
//------------------------------------------------------------------------------
size_t TCustomIniFileStorage::ReadBinaryData(const UnicodeString & Name,
  void * Buffer, size_t Size)
{
  CCALLSTACK(TRACE_READING);
  CTRACEFMT(TRACE_READING, "[%s] [%s]", GetCurrentSection().c_str(), Name.c_str());
  RawByteString Value = HexToBytes(ReadStringRaw(Name, L""));
  size_t Len = Value.Length();
  if (Size > Len)
  {
    Size = Len;
  }
  assert(Buffer);
  memcpy(Buffer, Value.c_str(), Size);
  return Size;
}
//------------------------------------------------------------------------------
void TCustomIniFileStorage::WriteBool(const UnicodeString & Name, bool Value)
{
  FIniFile->WriteBool(GetCurrentSection(), MungeIniName(Name), Value);
}
//------------------------------------------------------------------------------
void TCustomIniFileStorage::WriteInteger(const UnicodeString & Name, int Value)
{
  FIniFile->WriteInteger(GetCurrentSection(), MungeIniName(Name), Value);
}
//------------------------------------------------------------------------------
void TCustomIniFileStorage::WriteInt64(const UnicodeString & Name, __int64 Value)
{
  WriteStringRaw(Name, IntToStr(Value));
}
//------------------------------------------------------------------------------
void TCustomIniFileStorage::WriteDateTime(const UnicodeString & Name, TDateTime Value)
{
  WriteBinaryData(Name, &Value, sizeof(Value));
}
//------------------------------------------------------------------------------
void TCustomIniFileStorage::WriteFloat(const UnicodeString & Name, double Value)
{
  WriteBinaryData(Name, &Value, sizeof(Value));
}
//------------------------------------------------------------------------------
void TCustomIniFileStorage::WriteStringRaw(const UnicodeString & Name, const UnicodeString & Value)
{
  FIniFile->WriteString(GetCurrentSection(), MungeIniName(Name), Value);
}
//------------------------------------------------------------------------------
void TCustomIniFileStorage::WriteBinaryData(const UnicodeString & Name,
  const void * Buffer, size_t Size)
{
  WriteStringRaw(Name, BytesToHex(RawByteString(static_cast<const char*>(Buffer), Size)));
}
//===========================================================================
TIniFileStorage::TIniFileStorage(const UnicodeString & AStorage):
  TCustomIniFileStorage(AStorage, new TMemIniFile(AStorage))
{
  CALLSTACK;
  TRACEFMT("1 [%s]", GetStorage().c_str());
  FOriginal = new TStringList();
  dynamic_cast<TMemIniFile *>(FIniFile)->GetString(FOriginal);
//!CLEANBEGIN
  for (intptr_t Index = 0; Index < FOriginal->GetCount(); ++Index)
  {
    TRACEFMT("ini [%s]", FOriginal->Strings[Index].c_str());
  }
//!CLEANEND
  ApplyOverrides();
  TRACE("/");
}
//------------------------------------------------------------------------------
void TIniFileStorage::Flush()
{
  CALLSTACK;
  if (FOriginal != NULL)
  {
    TStrings * Strings = new TStringList;
    TRY_FINALLY (
    {
      TRACE("0");
      dynamic_cast<TMemIniFile *>(FIniFile)->GetString(Strings);
      TRACE("1");
      if (!Strings->Equals(FOriginal))
      {
        TRACE("2");
        int Attr;
        // preserve attributes (especially hidden)
        bool Exists = FileExists(GetStorage());
        if (Exists)
        {
          TRACE("3");
          Attr = GetFileAttributes(UnicodeString(GetStorage()).c_str());
        }
        else
        {
          Attr = FILE_ATTRIBUTE_NORMAL;
        }

        HANDLE Handle = CreateFile(UnicodeString(GetStorage()).c_str(), GENERIC_READ | GENERIC_WRITE,
          0, NULL, CREATE_ALWAYS, Attr, 0);

        if (Handle == INVALID_HANDLE_VALUE)
        {
          TRACE("4");
          // "access denied" errors upon implicit saves to existing file are ignored
          if (GetExplicit() || !Exists || (GetLastError() != ERROR_ACCESS_DENIED))
          {
            TRACE("5");
            try
            {
              TRACE("6");
              RaiseLastOSError();
            }
            catch(Exception & E)
            {
              TRACE("7");
              throw ExtException(&E, FMTLOAD(CREATE_FILE_ERROR, (GetStorage())));
            }
          }
        }
        else
        {
          TRACE("8");
          TStream * Stream = new THandleStream(Handle);
          TRY_FINALLY (
          {
            Strings->SaveToStream(Stream);
          }
          ,
          {
            TRACE("9");
            ::CloseHandle(Handle);
            delete Stream;
          }
          );
        }
      }
    }
    ,
    {
      TRACE("10");
      delete FOriginal;
      FOriginal = NULL;
      TRACE("11");
      delete Strings;
      TRACE("12");
    }
    );
    TRACE("/");
  }
}
//------------------------------------------------------------------------------
TIniFileStorage::~TIniFileStorage()
{
  Flush();
}
//------------------------------------------------------------------------------
void TIniFileStorage::ApplyOverrides()
{
  CALLSTACK;
  UnicodeString OverridesKey = IncludeTrailingBackslash(L"Override");

  TStrings * Sections = new TStringList();
  TRY_FINALLY (
  {
    Sections->Clear();
    FIniFile->ReadSections(Sections);
    for (int i = 0; i < Sections->GetCount(); i++)
    {
      UnicodeString Section = Sections->Strings[i];

      if (AnsiSameText(OverridesKey,
            Section.SubString(1, OverridesKey.Length())))
      {
        UnicodeString SubKey = Section.SubString(OverridesKey.Length() + 1,
          Section.Length() - OverridesKey.Length());

        // this all uses raw names (munged)
        TStrings * Names = new TStringList;
        TRY_FINALLY (
        {
          FIniFile->ReadSection(Section, Names);

          for (int ii = 0; ii < Names->GetCount(); ii++)
          {
            UnicodeString Name = Names->Strings[ii];
            UnicodeString Value = FIniFile->ReadString(Section, Name, L"");
            FIniFile->WriteString(SubKey, Name, Value);
          }
        }
        ,
        {
          delete Names;
        }
        );

        FIniFile->EraseSection(Section);
      }
    }
  }
  ,
  {
    delete Sections;
  }
  );
  TRACE("/");
}
//===========================================================================
#define NOT_IMPLEMENTED throw Exception("Not implemented")
//===========================================================================
class TOptionsIniFile : public TCustomIniFile
{
public:
  TOptionsIniFile(TStrings * Options);

  virtual UnicodeString ReadString(const UnicodeString & Section, const UnicodeString & Ident, const UnicodeString & Default);
  virtual void WriteString(const UnicodeString & Section, const UnicodeString & Ident, const UnicodeString & Value);
  virtual void ReadSection(const UnicodeString & Section, TStrings * Strings);
  virtual void ReadSections(TStrings* Strings);
  virtual void ReadSectionValues(const UnicodeString & Section, TStrings* Strings);
  virtual void EraseSection(const UnicodeString & Section);
  virtual void DeleteKey(const UnicodeString & Section, const UnicodeString & Ident);
  virtual void UpdateFile();
  // Hoisted overload
  void ReadSections(const UnicodeString & Section, TStrings* Strings);

private:
  TStrings * FOptions;
};
//------------------------------------------------------------------------------
TOptionsIniFile::TOptionsIniFile(TStrings * Options) :
  TCustomIniFile(UnicodeString())
{
  FOptions = Options;
}
//------------------------------------------------------------------------------
UnicodeString TOptionsIniFile::ReadString(const UnicodeString & Section, const UnicodeString & Ident, const UnicodeString & Default)
{
  assert(Section.IsEmpty());
  int Index = FOptions->IndexOfName(Ident);
  UnicodeString Value;
  if (Index >= 0)
  {
    Value = FOptions->ValueFromIndex[Index];
  }
  else
  {
    Value = Default;
  }
  return Value;
}
//------------------------------------------------------------------------------
void TOptionsIniFile::WriteString(const UnicodeString & Section, const UnicodeString & Ident, const UnicodeString & Value)
{
  NOT_IMPLEMENTED;
}
//------------------------------------------------------------------------------
void TOptionsIniFile::ReadSection(const UnicodeString & Section, TStrings * Strings)
{
  assert(Section.IsEmpty());
  Strings->BeginUpdate();

  TRY_FINALLY (
  {
    for (intptr_t Index = 0; Index < FOptions->GetCount(); ++Index)
    {
      Strings->Add(FOptions->Names[Index]);
    }
  }
  ,
  {
    Strings->EndUpdate();
  }
  );
}
//------------------------------------------------------------------------------
void TOptionsIniFile::ReadSections(TStrings * /*Strings*/)
{
  NOT_IMPLEMENTED;
}
//------------------------------------------------------------------------------
void TOptionsIniFile::ReadSectionValues(const UnicodeString & Section, TStrings * /*Strings*/)
{
  NOT_IMPLEMENTED;
}
//------------------------------------------------------------------------------
void TOptionsIniFile::EraseSection(const UnicodeString & Section)
{
  NOT_IMPLEMENTED;
}
//------------------------------------------------------------------------------
void TOptionsIniFile::DeleteKey(const UnicodeString & Section, const UnicodeString & Ident)
{
  NOT_IMPLEMENTED;
}
//------------------------------------------------------------------------------
void TOptionsIniFile::UpdateFile()
{
  NOT_IMPLEMENTED;
}
//------------------------------------------------------------------------------
void TOptionsIniFile::ReadSections(const UnicodeString & Section, TStrings* Strings)
{
  TCustomIniFile::ReadSections(Section, Strings);
}
//===========================================================================
TOptionsStorage::TOptionsStorage(TStrings * Options) :
  TCustomIniFileStorage(UnicodeString(L"Command-line options")) // , new TOptionsIniFile(Options))
{
  CALLSTACK;
}
#endif
