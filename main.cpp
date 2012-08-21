#include <boost/program_options.hpp>
#include <boost/filesystem.hpp>
namespace po = boost::program_options;

#include <iostream>
#include <vector>
#include <string>
#include <fstream>
#include "SolverManager.h"
#include <signal.h>
using namespace std;

//Global store for variables
po::variables_map vm;
bool verbose;
const char NSOLV[] = "nsolv";
SolverManager* sm=NULL;
struct sigaction act;

//Parses command line options and config file.
void parseOptions(int argc, char* argv[]);

void printHelp(po::options_description& o);

void handleExit(int signum);

int main(int ac, char* av[])
{
	/* We want to prevent SIGINT, SIGTERM & SIGQUIT
	 * from interrupting the instantiation (parseOptions)
	 * process so we temporary block them.
	 */
	int result=0;

	memset(&act,0,sizeof(act));
	act.sa_handler = SIG_IGN;

	result=sigaction(SIGTERM,&act,NULL);
	if(result == -1) cerr << "Couldn't block SIGTERM" << endl;
	result=sigaction(SIGQUIT,&act,NULL);
	if(result == -1) cerr << "Couldn't block SIGQUIT" << endl;
	result=sigaction(SIGINT,&act,NULL);
	if(result == -1) cerr << "Couldn't block SIGINT" << endl;

	parseOptions(ac,av);

	/* Now that the SolverManager is instantiated it is safe
	 * to allow the user to force an early exit.
	 */
	act.sa_handler = handleExit;
	result=sigaction(SIGTERM,&act,NULL);
	if(result == -1) cerr << "Couldn't setup handler for SIGTERM" << endl;
	result=sigaction(SIGQUIT,&act,NULL);
	if(result == -1) cerr << "Couldn't setup handler for SIGQUIT" << endl;
	result=sigaction(SIGINT,&act,NULL);
	if(result == -1) cerr << "Couldn't setup handler for SIGQUIT" << endl;


	sm->invokeSolvers();

	delete sm;
    return 0;
}

void parseOptions(int argc, char* argv[])
{
	vector< string> solverList;

	try
	{
		/* Declare the different group of program
		 * options available
		 */
		po::options_description generalOpts("General options");
		generalOpts.add_options()
				("help,h", "produce help message")
				("config,c", po::value<std::string>()->default_value("./nsolv.cfg"), "Path to configuration file.")

				;


		po::options_description solverOpts("Solver options");
		solverOpts.add_options()
				("solver,s", po::value< std::vector<string> >(&solverList)->composing(),
						"Specify a solver to use. This option can be set multiple times so that each "
						"solver is invoked in a different process.")
				("timeout,t", po::value<double>()->default_value(0.0), "Set timeout in seconds.")
				("verbose", po::value<bool>(&verbose)->default_value(true), "Print running information to standard error.")
				;


		//This is used as a positional argument
		po::options_description input("Input");
		input.add_options()("input", po::value<std::string>()->required(),"Specifies SMTLIBv2 input file.");
		po::positional_options_description p;
		p.add("input",1);

		//Create options group to show for help messages
		po::options_description hm("Options");
		hm.add(generalOpts).add(solverOpts);

		//Options group for parsing
		po::options_description cl("");
		cl.add(generalOpts).add(solverOpts).add(input);

		//invoke commandline parser
		po::store(po::command_line_parser(argc, argv).
				  options(cl).positional(p).run(), vm);

		/*check for help option first because the input might not be set
		* which will cause notify() to throw an exception
		*/
		if(vm.count("help"))
			printHelp(hm);

		po::notify(vm);//trigger exceptions if there are any

		//check input file exists
		boost::filesystem::path inputFile(vm["input"].as<string>());
		if(! boost::filesystem::is_regular_file(inputFile))
		{
			cerr << "Error: Input SMTLIBv2 file (" << vm["input"].as<string>() <<
					") does not exist or is not a regular file." << endl;
			exit(1);
		}


		//if the configuration file exists then load it
		boost::filesystem::path configFile(vm["config"].as<string>());
		bool configFileExists=boost::filesystem::is_regular_file(configFile);


		//If user manually specified a config file check it exists
		if(vm.count("config") > 0 && ! configFileExists )
		{
			cerr << "Error: Configuration file " << configFile.generic_string() << " does not exist!" << endl;
			exit(1);
		}

		po::options_description indivSolvOpt("");
		if(configFileExists)
		{
			//parse configuration file for options too
			if(verbose) cerr << "Parsing configuration file:" << configFile.generic_string() << endl;

			ifstream cf;
			cf.open(configFile.c_str());

			if(!cf.good())
			{
				cerr << "Couldn't open configuration file " << configFile.generic_string() << endl;
			}


			//Do first pass for solver options
			po::store(po::parse_config_file(cf,solverOpts,true), vm);
			po::notify(vm);

			cf.close();
			cf.open(configFile.c_str());

			//cf.seekg(0,ios_base::beg);//Move to the beginning of the file.

			/*loop over solvers and create
			 * <solvername>.opts options
			 * <solvername>.input-on-stdin options
			 */
			for(vector<string>::const_iterator s= solverList.begin(); s != solverList.end(); ++s)
			{
				string optionName(*s);

				//Do <solvername>.opts
				optionName+=".opts";
				indivSolvOpt.add_options() (optionName.c_str(),po::value<string>(),"");
				if(verbose) cerr << "Looking for \"" << optionName << "\" in " << configFile << endl;

				//Do <solvername>.input-on-stdout
				optionName=*s;
				optionName+=".input-on-stdin";
				indivSolvOpt.add_options() (optionName.c_str(),po::value<bool>()->default_value(false),"");
				if(verbose) cerr << "Looking for \"" << optionName << "\" in " << configFile << endl;

			}

			//Do second pass for per solver options
			po::store(po::parse_config_file(cf,indivSolvOpt,true), vm);
			po::notify(vm);

			cf.close();
		}

		try {sm = new SolverManager(vm["input"].as<string>(),vm["timeout"].as<double>());}
		catch(std::bad_alloc& e)
		{
			cerr << "Failed to allocate memory of SolverManager:" << e.what() << endl;
			exit(1);
		}

		//Now finally create solvers
		for(vector<string>::const_iterator s= solverList.begin(); s != solverList.end(); ++s)
		{
			string solvOpt(*s);
			solvOpt+=".opts";

			string stdinOpt(*s);
			stdinOpt+=".input-on-stdin";

			bool inputOnStdin = false;
			if(configFileExists && vm.count(stdinOpt.c_str()) && vm[stdinOpt.c_str()].as<bool>() )
				inputOnStdin=true;

			if(configFileExists && vm.count(solvOpt.c_str()))
			{
				sm->addSolver(*s, vm[solvOpt.c_str()].as<string>(), inputOnStdin);
			}
			else
				sm->addSolver(*s,inputOnStdin);

		}


	}
	catch(po::required_option& r)
	{
		cerr << "Error: ";
		if (r.get_option_name() == "--input")
		{
			cerr << "Input SMTLIBv2 file must be specified." << endl;
		}
		else
		{
			cerr << r.what() << endl;
		}

		exit(1);
	}
	catch(exception& e)
	{
		cerr << "Error:" << e.what() << endl;
		exit(1);
	}


}

void printHelp(po::options_description& o)
{
	cout << NSOLV << " [options] <input>" << endl <<
			"<input> is a valid (.smt2) SMTLIBv2 file." << endl << endl <<

			"NSolv allows several SMTLIBv2 solvers to be invoked simultaneously (each as a separate process)." << endl <<
			"Multiple calls to --solver will create each solver. It also possible (and recommended) to specify this " << endl <<
			"in a configuration file. Command line parameters for each solver may also be specified in " << endl <<
			"the configuration file but NOT on the command line of NSolv. It also possible to specify how <input> " << endl <<
			"is given to each solver (either as the last command line parameter or on standard input) in the configuration " << endl <<
			"file." << endl << endl <<

			"CONFIGURATION FILE FORMAT" << endl <<
			"Here is an example..." << endl << endl <<
			"-------------------------------------------------------------------------------" << endl <<
			"#This is a comment" << endl <<
			"solver = z3" << endl <<
			"#Set command line options to be passed to z3 solver" << endl <<
			"z3.opts = -smt2 -v:0" << endl << endl <<

			"solver = mathsat" << endl <<
			"#Set command line options to be passed to mathsat solver" << endl <<
			"mathsat.opts = -input=smt2 -verbosity=0" << endl <<
			"#Set the input file to be passed to mathsat on standard input" << endl <<
			"mathsat.input-on-stdin = on" << endl << endl <<

			"#Set the timeout in seconds" << endl <<
			"timeout = 60.0" << endl << endl <<
			"#Switch off NSolv's verbose output" << endl <<
			"verbose = off" << endl <<
			"-------------------------------------------------------------------------------" << endl << endl <<

			"Each solver must be declared on a separate line as shown above. Options can specified for " << endl <<
			"each solver by adding a line starting with \"<solver-name>.opts =\". These options are space separated." << endl <<
			"Quotes (\") are interpreted literally so it is not possible to have a single argument with a space in." << endl <<
			"Whether or not the <input> is given to a particular solver on standard input can be controlled by adding the line " << endl <<
			"starting with \"<solver-name>.input-on-stdin =\". The default behaviour is to pass <input> as the last command " << endl <<
			"line parameter to the solver." << endl << endl <<
			"The --solver <name> option and \"Solver = <name>\" option in the configuration file use <name> as the " << endl <<
			"solver name but also as the executable name. Therefore <name> should be in your PATH." << endl << endl;


	cout << o << endl;
	exit(0);
}

void handleExit(int signum)
{
	int result=0;
	if(verbose) cerr << "Received signal " << signum << ". Trying to cleanly exit..." << endl;

	/*Assume the Solver Manager has already been created and not already deleted.
	 * There is a possible race condition here if sm was already deleted. FIXME
	 */
	delete sm;

	//Remove signal handler for signals.
	act.sa_handler = SIG_DFL;
	result=sigaction(SIGTERM,&act,NULL);
	if(result == -1) cerr << "Couldn't set SIGTERM handler to default" << endl;
	result=sigaction(SIGQUIT,&act,NULL);
	if(result == -1) cerr << "Couldn't set SIGQUIT handler to default" << endl;
	result=sigaction(SIGINT,&act,NULL);
	if(result == -1) cerr << "Couldn't set SIGINT handler to default" << endl;

	//Now send the received signal to ourself.
	kill(getpid(),signum);
}
