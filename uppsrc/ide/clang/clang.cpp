#include "clang.h"

#define LLOG(x)
#define LTIMESTOP(x) TIMESTOP(x)

String FetchString(CXString cs)
{
	if(!HasLibClang())
		return Null;
	String result = clang_getCString(cs);
	clang_disposeString(cs);
	return result;
}

String GetCursorKindName(CXCursorKind cursorKind)
{
	if(!HasLibClang())
		return Null;
	return FetchString(clang_getCursorKindSpelling(cursorKind));
}

String GetCursorSpelling(CXCursor cursor)
{
	if(!HasLibClang())
		return Null;
	return FetchString(clang_getCursorSpelling(cursor));
}

String GetTypeSpelling(CXCursor cursor)
{
	if(!HasLibClang())
		return Null;
	return FetchString(clang_getTypeSpelling(clang_getCursorType(cursor)));
}

String GetClangInternalIncludes()
{
	static String includes;
	ONCELOCK {
		String dummy = ConfigFile("dummy.cpp");
		Upp::SaveFile(dummy, String());
		String h = Sys(
		#ifdef PLATFORM_WIN32
				GetExeDirFile("bin/clang/bin/c++") +
		#else
				"clang++"
		#endif
				" -v -x c++ -E " + dummy
		);
		DeleteFile(dummy);
		h.Replace("\r", "");
		Vector<String> ln = Split(h, '\n');
		for(int i = 0; i < ln.GetCount(); i++) {
			String dir = TrimBoth(ln[i]);
			if(DirectoryExists(dir))
				MergeWith(includes, ";", NormalizePath(dir));
		}
	}
	return includes;
}

void Clang::Dispose()
{
	if(!HasLibClang())
		return;
	if(tu) {
		INTERLOCKED { // Otherwise dispose takes much longer, probably due to clang allocator lock contention
//			TIMESTOP("clang_disposeTranslationUnit");
			clang_disposeTranslationUnit(tu);
		}
	}
	tu = nullptr;
}

bool Clang::Parse(const String& filename, const String& content, const String& includes_, const String& defines, dword options)
{
	if(!HasLibClang())
		return false;

	MemoryIgnoreLeaksBlock __;
	if(!index) return false;
	
//	LTIMESTOP("Parse " << filename << " " << includes_ << " " << defines);
	
	Dispose();

	String cmdline;

	cmdline << filename << " -DflagDEBUG -DflagDEBUG_FULL -DflagMAIN -DflagCLANG -xc++ -std=c++17 ";
	
	cmdline << RedefineMacros();
	
	String includes = includes_;
	MergeWith(includes, ";", GetClangInternalIncludes());

	Vector<String> args;
	for(const String& s : Split(includes, ';'))
		args.Add("-I" + s);

	for(const String& s : Split(defines + ";CLANG", ';'))
		args.Add("-D" + s);

	args.Append(Split(cmdline, ' '));
	
	Vector<const char *> argv;

	for(const String& s : args)
		argv.Add(~s);

	CXUnsavedFile ufile = { ~filename, ~content, (unsigned)content.GetCount() };
	tu = clang_parseTranslationUnit(index, nullptr, argv, argv.GetCount(),
	                                options & PARSE_FILE ? nullptr : &ufile,
	                                options & PARSE_FILE ? 0 : 1,
	                                options);

//	DumpDiagnostics(tu);
	
	return tu;
}

bool Clang::ReParse(const String& filename, const String& content)
{
	if(!HasLibClang())
		return false;

	MemoryIgnoreLeaksBlock __;
	CXUnsavedFile ufile = { ~filename, ~content, (unsigned)content.GetCount() };
	if(clang_reparseTranslationUnit(tu, 1, &ufile, 0)) {
		Dispose();
		return false;
	}
	return true;
}

Clang::Clang()
{
	if(!HasLibClang())
		return;

	MemoryIgnoreLeaksBlock __;
	index = clang_createIndex(0, 0);
}

Clang::~Clang()
{
	if(!HasLibClang())
		return;

	MemoryIgnoreLeaksBlock __;
	Dispose();
	clang_disposeIndex(index);
}

void DumpDiagnostics(CXTranslationUnit tu)
{
	if(!HasLibClang())
		return;

	size_t num_diagnostics = clang_getNumDiagnostics(tu);

	for (size_t i = 0; i < num_diagnostics; ++i) {
		CXDiagnostic diagnostic = clang_getDiagnostic(tu, i);
		auto Dump = [&](CXDiagnostic diagnostic) {
			CXFile file;
			unsigned line;
			unsigned column;
			unsigned offset;
			CXSourceLocation location = clang_getDiagnosticLocation(diagnostic);
			clang_getExpansionLocation(location, &file, &line, &column, &offset);
			LOG(FetchString(clang_getFileName(file)) << " (" << line << ":" << column << ") " <<
				FetchString(clang_getDiagnosticSpelling(diagnostic)));
		};
		Dump(diagnostic);
	#if 0
		CXDiagnosticSet set = clang_getChildDiagnostics(diagnostic);
		int n = clang_getNumDiagnosticsInSet(set);
		for(int i = 0; i < n; i++) {
			CXDiagnostic d = clang_getDiagnosticInSet(set, i);
			Dump(d);
			clang_disposeDiagnostic(d);
		}
	#endif
		clang_disposeDiagnostic(diagnostic);
	}
}