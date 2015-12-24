#include "PDBExtractor.h"
#include "PDB.h"
#include "PDBHeaderReconstructor.h"
#include "PDBSymbolVisitor.h"
#include "PDBSymbolSorter.h"
#include "UserDataFieldDefinition.h"

#include <iostream>
#include <fstream>
#include <stdexcept>

namespace
{
	//
	// Headers & footers for test file and reconstructed header.
	//

	static const char TEST_FILE_HEADER[] =
		"#include <stdio.h>\n"
		"#include <stddef.h>\n"
		"#include <stdint.h>\n"
		"\n"
		"#include \"%s\"\n"
		"\n"
		"int main()\n"
		"{\n";

	static const char TEST_FILE_FOOTER[] =
		"\n"
		"\treturn 0;\n"
		"}\n"
		"\n";

	static const char HEADER_FILE_HEADER[] =
		"/*\n"
		" * PDB file: %s\n"
		" * Image architecture: %s\n"
		" *\n"
		" * Dumped by pdbex tool v" PDBEX_VERSION_STRING ", by wbenny\n"
		" */\n\n";

	//
	// Error messages.
	//

	static const char* MESSAGE_INVALID_PARAMETERS =
		"Invalid parameters";

	static const char* MESSAGE_FILE_NOT_FOUND =
		"File not found";

	static const char* MESSAGE_SYMBOL_NOT_FOUND =
		"Symbol not found";

	//
	// Our exception class.
	//

	class PDBDumperException
		: public std::runtime_error
	{
		public:
			PDBDumperException(const char* Message)
				: std::runtime_error(Message)
			{

			}
	};
}

int
PDBExtractor::Run(
	int argc,
	char** argv
	)
{
	int Result = ERROR_SUCCESS;

	try
	{
		ParseParameters(argc, argv);
		OpenPDBFile();

		PrintTestHeader();

		if (m_Settings.SymbolName == "*")
		{
			DumpAllSymbols();
		}
		else
		{
			DumpOneSymbol();
		}

		PrintTestFooter();
	}
	catch (PDBDumperException& e)
	{
		std::cerr << e.what() << std::endl;
		Result = EXIT_FAILURE;
	}

	CloseOpenedFiles();

	return Result;
}

void
PDBExtractor::PrintUsage()
{
	printf("Extracts types and structures from PDB (Program database).\n");
	printf("Version v%s\n", PDBEX_VERSION_STRING);
	printf("\n");
	printf("pdbex <symbol> <path> [-o <filename>] [-t <filename>] [-e <type>]\n");
	printf("                     [-u <prefix>] [-s prefix] [-r prefix] [-g suffix]\n");
	printf("                     [-p] [-x] [-m] [-b] [-d] [-i] [-l]\n");
	printf("\n");
	printf("<symbol>             Symbol name to extract or '*' if all symbol should\n");
	printf("                     be extracted.\n");
	printf("<path>               Path to the PDB file.\n");
	printf(" -o filename         Specifies the output file.                       (stdout)\n");
	printf(" -t filename         Specifies the output test file.                  (off)\n");
	printf(" -e [n,i,a]          Specifies expansion of nested structures/unions. (i)\n");
	printf("                       n = none            Only top-most type is printed.\n");
	printf("                       i = inline unnamed  Unnamed types are nested.\n");
	printf("                       a = inline all      All types are nested.\n");
	printf(" -u prefix           Unnamed union prefix  (in combination with -d).\n");
	printf(" -s prefix           Unnamed struct prefix (in combination with -d).\n");
	printf(" -r prefix           Prefix for all symbols.\n");
	printf(" -g suffix           Suffix for all symbols.\n");
	printf("\n");
	printf("Following options can be explicitly turned of by leading '-'.\n");
	printf("Example: -p-\n");
	printf(" -p                  Create padding members.                          (T)\n");
	printf(" -x                  Show offsets.                                    (T)\n");
	printf(" -m                  Create Microsoft typedefs.                       (T)\n");
	printf(" -b                  Allow bitfields in union.                        (F)\n");
	printf(" -d                  Allow unnamed data types.                        (T)\n");
	printf(" -i                  Use types from stdint.h instead of native types. (F)\n");
	printf(" -j                  Print definitions of referenced types.           (T)\n");
	printf(" -k                  Print header.                                    (T)\n");
	printf(" -n                  Print declarations.                              (T)\n");
	printf(" -l                  Print definitions.                               (T)\n");
	printf("\n");
}

void
PDBExtractor::ParseParameters(
	int argc,
	char** argv
	)
{
	//
	// Early check for help parameter.
	//

	if ( argc == 1 ||
	    (argc == 2 && strcmp(argv[1], "-h") == 0) ||
	    (argc == 2 && strcmp(argv[1], "--help") == 0))
	{
		PrintUsage();

		//
		// Kitten died when I wrote this.
		//

		exit(EXIT_SUCCESS);
	}

	int ArgumentPointer = 0;

	m_Settings.SymbolName = argv[++ArgumentPointer];
	m_Settings.PdbPath = argv[++ArgumentPointer];

	while (++ArgumentPointer < argc)
	{
		const char* CurrentArgument = argv[ArgumentPointer];
		size_t CurrentArgumentLength = strlen(CurrentArgument);

		const char* NextArgument = ArgumentPointer < argc
			? argv[ArgumentPointer + 1]
			: nullptr;

		size_t NextArgumentLength = NextArgument
			? strlen(CurrentArgument)
			: 0;

		//
		// Handling of -X- switches.
		//

		if ((CurrentArgumentLength != 2 && CurrentArgumentLength != 3) ||
		    (CurrentArgumentLength == 2 && CurrentArgument[0] != '-') ||
		    (CurrentArgumentLength == 3 && CurrentArgument[0] != '-' && CurrentArgument[2] != '-'))
		{
			throw PDBDumperException(MESSAGE_INVALID_PARAMETERS);
		}

		bool OffSwitch = CurrentArgumentLength == 3 && CurrentArgument[2] == '-';

		//
		// Handling of options.
		//

		switch (CurrentArgument[1])
		{
			case 'o':
				if (!NextArgument)
				{
					throw PDBDumperException(MESSAGE_INVALID_PARAMETERS);
				}

				++ArgumentPointer;
				m_Settings.OutputFilename = NextArgument;
				m_Settings.PdbHeaderReconstructorSettings.OutputFile = new std::ofstream(
					NextArgument,
					std::ios::out
					);
				break;

			case 't':
				if (!NextArgument)
				{
					throw PDBDumperException(MESSAGE_INVALID_PARAMETERS);
				}

				++ArgumentPointer;
				m_Settings.TestFilename = NextArgument;
				m_Settings.PdbHeaderReconstructorSettings.TestFile = new std::ofstream(
					m_Settings.TestFilename,
					std::ios::out
					);

				break;

			case 'e':
				if (!NextArgument)
				{
					throw PDBDumperException(MESSAGE_INVALID_PARAMETERS);
				}

				++ArgumentPointer;
				switch (NextArgument[0])
				{
					case 'n':
						m_Settings.PdbHeaderReconstructorSettings.MemberStructExpansion =
							PDBHeaderReconstructor::MemberStructExpansionType::None;
						break;

					case 'i':
						m_Settings.PdbHeaderReconstructorSettings.MemberStructExpansion =
							PDBHeaderReconstructor::MemberStructExpansionType::InlineUnnamed;
						break;

					case 'a':
						m_Settings.PdbHeaderReconstructorSettings.MemberStructExpansion =
							PDBHeaderReconstructor::MemberStructExpansionType::InlineAll;
						break;

					default:
						m_Settings.PdbHeaderReconstructorSettings.MemberStructExpansion =
							PDBHeaderReconstructor::MemberStructExpansionType::InlineUnnamed;
						break;
				}
				break;

			case 'u':
				if (!NextArgument)
				{
					throw PDBDumperException(MESSAGE_INVALID_PARAMETERS);
				}

				++ArgumentPointer;
				m_Settings.PdbHeaderReconstructorSettings.AnonymousUnionPrefix = NextArgument;
				break;

			case 's':
				if (!NextArgument)
				{
					throw PDBDumperException(MESSAGE_INVALID_PARAMETERS);
				}

				++ArgumentPointer;
				m_Settings.PdbHeaderReconstructorSettings.AnonymousStructPrefix = NextArgument;
				break;

			case 'r':
				if (!NextArgument)
				{
					throw PDBDumperException(MESSAGE_INVALID_PARAMETERS);
				}

				++ArgumentPointer;
				m_Settings.PdbHeaderReconstructorSettings.SymbolPrefix = NextArgument;
				break;

			case 'g':
				if (!NextArgument)
				{
					throw PDBDumperException(MESSAGE_INVALID_PARAMETERS);
				}

				++ArgumentPointer;
				m_Settings.PdbHeaderReconstructorSettings.SymbolSuffix = NextArgument;
				break;

			case 'p':
				m_Settings.PdbHeaderReconstructorSettings.CreatePaddingMembers = !OffSwitch;
				break;

			case 'x':
				m_Settings.PdbHeaderReconstructorSettings.ShowOffsets = !OffSwitch;
				break;

			case 'm':
				m_Settings.PdbHeaderReconstructorSettings.MicrosoftTypedefs = !OffSwitch;
				break;

			case 'b':
				m_Settings.PdbHeaderReconstructorSettings.AllowBitFieldsInUnion = !OffSwitch;
				break;

			case 'd':
				m_Settings.PdbHeaderReconstructorSettings.AllowAnonymousDataTypes = !OffSwitch;
				break;

			case 'i':
				m_Settings.UserDataFieldDefinitionSettings.UseStdInt = !OffSwitch;
				break;

			case 'j':
				m_Settings.PrintReferencedTypes = !OffSwitch;
				break;

			case 'k':
				m_Settings.PrintHeader = !OffSwitch;
				break;

			case 'n':
				m_Settings.PrintDeclarations = !OffSwitch;
				break;

			case 'l':
				m_Settings.PrintDefinitions = !OffSwitch;
				break;

			default:
				throw PDBDumperException(MESSAGE_INVALID_PARAMETERS);
		}
	}

	m_HeaderReconstructor = std::make_unique<PDBHeaderReconstructor>(
		&m_Settings.PdbHeaderReconstructorSettings
		);

	m_SymbolVisitor = std::make_unique<PDBSymbolVisitor<UserDataFieldDefinition>>(
		m_HeaderReconstructor.get(),
		&m_Settings.UserDataFieldDefinitionSettings
		);

	m_SymbolSorter = std::make_unique<PDBSymbolSorter>();
}

void
PDBExtractor::OpenPDBFile()
{
	if (m_PDB.Open(m_Settings.PdbPath.c_str()) == FALSE)
	{
		throw PDBDumperException(MESSAGE_FILE_NOT_FOUND);
	}
}

void
PDBExtractor::PrintTestHeader()
{
	if (m_Settings.PdbHeaderReconstructorSettings.TestFile != nullptr)
	{
		static char TEST_FILE_HEADER_FORMATTED[16 * 1024];
		sprintf_s(
			TEST_FILE_HEADER_FORMATTED, TEST_FILE_HEADER,
			m_Settings.OutputFilename
			);

		(*m_Settings.PdbHeaderReconstructorSettings.TestFile) << TEST_FILE_HEADER_FORMATTED;
	}
}

void
PDBExtractor::PrintTestFooter()
{
	if (m_Settings.PdbHeaderReconstructorSettings.TestFile != nullptr)
	{
		(*m_Settings.PdbHeaderReconstructorSettings.TestFile) << TEST_FILE_FOOTER;
	}
}

void
PDBExtractor::PrintPDBHeader()
{
	if (m_Settings.PrintHeader)
	{
		GetArchitecture();

		static char* ArchictureStrings[] = {
			"None",
			"x86",
			"x64"
		};

		static char HEADER_FILE_HEADER_FORMATTED[16 * 1024];

		sprintf_s(
			HEADER_FILE_HEADER_FORMATTED, HEADER_FILE_HEADER,
			m_Settings.PdbPath.c_str(),
			ArchictureStrings[(int)m_Architecture]
			);

		(*m_Settings.PdbHeaderReconstructorSettings.OutputFile) << HEADER_FILE_HEADER_FORMATTED;
	}
}

void
PDBExtractor::PrintPDBDeclarations()
{
	//
	// Write declarations.
	//

	if (m_Settings.PrintDeclarations)
	{
		for (auto&& e : m_SymbolSorter->GetSortedSymbols())
		{
			if (e->Tag == SymTagUDT && !PDB::IsUnnamedSymbol(e))
			{
				*m_Settings.PdbHeaderReconstructorSettings.OutputFile
					<< PDB::GetUdtKindString(e->u.UserData.Kind)
					<< " " << m_HeaderReconstructor->GetCorrectedSymbolName(e) << ";"
					<< std::endl;
			}
		}

		*m_Settings.PdbHeaderReconstructorSettings.OutputFile << std::endl;
	}
}

void
PDBExtractor::PrintPDBDefinitions()
{
	//
	// Write definitions.
	//

	if (m_Settings.PrintDefinitions)
	{
		for (auto&& e : m_SymbolSorter->GetSortedSymbols())
		{
			bool Expand = true;

			//
			// Do not expand unnamed types, if they will be inlined.
			//

			if (m_Settings.PdbHeaderReconstructorSettings.MemberStructExpansion == PDBHeaderReconstructor::MemberStructExpansionType::InlineUnnamed &&
				  (e->Tag == SymTagEnum || e->Tag == SymTagUDT) &&
				  PDB::IsUnnamedSymbol(e))
			{
				Expand = false;
			}

			if (Expand)
			{
				m_SymbolVisitor->Run(e);
			}
		}
	}
}

void
PDBExtractor::GetArchitecture()
{
	for (auto&& e : m_PDB.GetSymbolMap())
	{
		m_SymbolSorter->Visit(e.second);

		if (m_SymbolSorter->GetImageArchitecture() != ImageArchitecture::None)
		{
			m_Architecture = m_SymbolSorter->GetImageArchitecture();

			m_SymbolSorter->Clear();
			break;
		}
	}
}

void
PDBExtractor::DumpAllSymbols()
{
	//
	// We are going to print all symbols.
	//

	PrintPDBHeader();

	for (auto&& e : m_PDB.GetSymbolMap())
	{
		m_SymbolSorter->Visit(e.second);
	}

	PrintPDBDeclarations();
	PrintPDBDefinitions();
}

void
PDBExtractor::DumpOneSymbol()
{
	const SYMBOL* Symbol = m_PDB.GetSymbolByName(m_Settings.SymbolName.c_str());

	if (Symbol == nullptr)
	{
		throw PDBDumperException(MESSAGE_SYMBOL_NOT_FOUND);
	}

	PrintPDBHeader();

	//
	// InlineAll supresses PrintReferencedTypes.
	//

	if (m_Settings.PrintReferencedTypes &&
	    m_Settings.PdbHeaderReconstructorSettings.MemberStructExpansion != PDBHeaderReconstructor::MemberStructExpansionType::InlineAll)
	{
		m_SymbolSorter->Visit(Symbol);

		//
		// Print header only when PrintReferencedTypes == true.
		//

		PrintPDBDefinitions();
	}
	else
	{
		//
		// Print only the specified symbol.
		//

		m_SymbolVisitor->Run(Symbol);
	}
}

void
PDBExtractor::CloseOpenedFiles()
{
	//
	// We want to free the memory only if the filename was specified,
	// because OutputFile or TestFile may be std::cout.
	//

	if (m_Settings.TestFilename)
	{
		delete m_Settings.PdbHeaderReconstructorSettings.TestFile;
	}

	if (m_Settings.OutputFilename)
	{
		delete m_Settings.PdbHeaderReconstructorSettings.OutputFile;
	}
}