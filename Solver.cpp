#include "Solver.h"
#include "global.h"
#include <signal.h>
#include <errno.h>
#include <unistd.h>
#include <iostream>
#include <cstdlib>
#include <cstdio>
#include <cstring>
#include <fcntl.h>

using namespace std;
Solver::Solver(const std::string& _name, const std::string& _cmdOptions, const std::string& _inputFile, bool _inputOnStdin) :
name(_name), cmdOptions(), inputFile(_inputFile) , argv(NULL), pid(0), inputOnStdin(_inputOnStdin), resultAlreadyRead(false),
numberOfBytesReadFromPipe(0)
{
	setupArguments(_cmdOptions,_inputFile);

	//Setup half duplex pipe.
	int result= pipe(this->fd);
	if(result == -1)
	{
		perror("Problem setting up pipe:");
		exit(1);
	}

	//Solver::exec() should be called in child after fork() so we'll close fd appropriately there.
	//Solver::setPID() should be called in parent after fork so we'll close fd appropriately there.
}

Solver::~Solver()
{
	//It is presumed this is called only in the parent.

	kill();
	delete [] argv;
	argv=NULL;

	//Try closing the read end of the pipe. It may have already been closed in dumpResult()
	close(fd[0]);
}

bool Solver::setPID(pid_t p)
{
	if(pid == 0)
	{
		pid =p;

		/* We're in parent and the child has already forked from us (hopefully).
		 * So we should now close the writing end of the file descriptor.
		 */
		int result=close(fd[1]);
		if(result == -1)
		{
			perror("Error closing file descriptor in parent.");
			exit(1);
		}


		return true;
	}
	else
		return false;
}

Solver::Result Solver::getResult()
{
	//Read the result from the child if we haven't already tried
	if(!resultAlreadyRead)
	{
			int result=::read(fd[0],buffer,sizeof(buffer));
			if(result == -1)
			{
				perror("read:");
				exit(1);
			}

		resultAlreadyRead=true;

		/* Ideally we would like to read sizeof(buffer) bytes, but the read()
		 * system call does NOT guarantee this! So we must record how many bytes it gave us.
		 * This might not actually be enough to check for (sat|unsat|unknown) but let's hope it is!
		 */
		numberOfBytesReadFromPipe=result;
	}

	//check for the valid responses from a SMTLIBv2 solver
	if(bufferMatch("sat"))
	{
		return Solver::SAT;
	}
	else if(bufferMatch("unsat"))
	{
		return Solver::UNSAT;
	}
	else if(bufferMatch("unknown"))
	{
		return Solver::UNKNOWN;
	}
	else
		return Solver::ERROR;
}

void Solver::dumpResult()
{
	if(resultAlreadyRead==false)
	{
		cerr << "Solver::dumpResult() . You need to call getResult() first!" << endl;
		return;
	}

	int result=0;

	//dump the buffer to stdout
	result=write(fileno(stdout),buffer,numberOfBytesReadFromPipe);
	if(result== -1)
	{
		cerr << "Solver::dumpResult() : Failed to write buffer to stdout." << endl;
		perror("Write:");
		return;
	}
	fflush(stdout);

	//print out what remains inside the pipe.
	FILE* f = fdopen(fd[0],"r");

	if(f == NULL)
	{
		cerr << "Solver::dumpResult() : Failed to open pipe to print remainder." << endl;
		return;
	}

	//Write what remains in the pipe to stdout.
	//Not very efficient but not sure how to do this a better way
	int c=fgetc(f);
	while(c != EOF)
	{
		putchar(c);
		c=fgetc(f);
	}
	fflush(stdout);
	result=fclose(f);
	if(result !=0)
	{
		cerr << "Solver::dumpResult() : Failed to close FILE* to pipe." << endl;
	}

}

void Solver::exec()
{
	//We should be in child after fork. We close the reading end of the pipe.
	int result=close(fd[0]);
	if(result == -1)
		perror("Problem closing file descriptor in child.");

	//We want stdout of the child to be sent to the parent via the pipe.
	result=dup2(fd[1],fileno(stdout));
	if(result == -1)
	{
		perror("Problem redirecting stdout of child to pipe!");
		exit(1);
	}

	if(inputOnStdin)
	{
		//The user wants us to send the SMTLIBv2 file on stdinput to the solver

		//get file descriptor.
		int smtlibFd=0;
		smtlibFd = ::open(inputFile.c_str(), O_RDONLY);

		if(smtlibFd == -1)
		{
			perror("Problem opening input SMTLIBv2 file:");
			exit(1);
		}

		//We want the stdinput for the solver to come from the input SMTLIBv2 file
		result=dup2(smtlibFd,fileno(stdin));
		if(result == -1)
		{
			perror("Problem redirecting input SMTLIBv2 file to stdinput:");
			exit(1);
		}

	}

	//Now execute the solver
	result = execvp(name.c_str(), (char * const*) argv);
	if(result == -1)
	{
		cerr << "Failed to execute solver:" << name << "!" << endl;
		perror("execvp:");
		exit(1);
	}
}

const std::string& Solver::toString()
{
	return name;
}

void Solver::kill()
{
	if(verbose) cerr << "Trying to kill solver " << name << " with pid:" << pid << endl;
	int result = ::kill(pid, SIGTERM);

	//Note ESRCH is when pid didn't exists, we don't care about that case.
	if(result == -1 && errno != ESRCH)
		cerr << "Killing process with PID:" << pid << " failed!" << endl;
}

void Solver::setupArguments(const std::string& cmdOptionsStr, const std::string& inputFile)
{

	string temp("");//Temporary token holder

	//push the argv[0] argument which is the program name.
	cmdOptions.push_back(name);

	string::const_iterator lastElement = cmdOptionsStr.end() -1;
	for(string::const_iterator c = cmdOptionsStr.begin(); c != cmdOptionsStr.end() ; ++c)
	{
		//ignore whitespace leading up to a token
		if(*c == ' ' && temp.length() ==0)
			continue;

		temp+=*c;

		//Hit end of token
		if( (c == lastElement || *(c +1) == ' ' ) && temp.length() > 0)
		{
			cmdOptions.push_back(temp);
			temp=""; //blank the temporary token holder
			continue;;
		}


	}

	//That last argument is the input file if that is what's requested for.
	if(!inputOnStdin)
	{
		cmdOptions.push_back(inputFile);
	}


	int index=0;

	if(verbose)
	{
		cerr << "Solver::setupArguments() : Found " << cmdOptions.size() << " argument(s) for solver " <<
				name << endl;

		for(vector<string>::const_iterator i =cmdOptions.begin(); i != cmdOptions.end(); ++i, ++index)
			cerr << "[" << index << "] = \"" << *i << "\"" << endl;
	}

	if(verbose && inputOnStdin)
		cerr << "Solver::setupArguments() : Input file (" << inputFile << ") will passed to solver " << name <<
		        " on standard input." << endl;

	/* We now need to setup a (char*) NULL terminated C
	 * array for execvp().
	 */
	try {argv = new const char* [cmdOptions.size() +1]; }
	catch(std::bad_alloc& e)
	{
		cerr << "Failed to allocate space for options array!" << endl;
		exit(1);
	}
	index=0;
	for(vector<string>::const_iterator i =cmdOptions.begin(); i != cmdOptions.end(); ++i, ++index)
		argv[index] = i->c_str();

	//Terminate with a NULL as execvp() expects
	argv[cmdOptions.size()] = (char*) NULL ;
}

int Solver::getReadFileDescriptor()
{
	return fd[0];
}

const char* Solver::resultToString(Solver::Result r)
{
	switch(r)
	{
		case SAT: return "sat";
		case UNSAT: return "unsat";
		case UNKNOWN: return "unknown";
		case ERROR:
		default:
			return "error";
	}
}

bool Solver::bufferMatch(const char cstring[])
{
	unsigned int strLength=strlen(cstring);

	if(strLength==0) return false; //can't deal with empty strings.

	for(unsigned int index=0; index < sizeof(buffer) && index < strLength; index++)
	{
		if(cstring[index] != buffer[index])
			return false;

		if(strLength == (index + 1))
			return true;// There was a match
	}

	return false;
}
