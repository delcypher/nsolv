#include "global.h"
#include "SolverManager.h"
#include <iostream>
#include <cmath>
#include <signal.h>
#include <cstdlib>
#include <cstring>
#include <errno.h>
#include <cstdio>
#include <sys/wait.h>
using namespace std;

////For SICHLD signal handler
//static SolverManager* instance=NULL;
//
//void sigChldHandler(int signum, siginfo_t* info, void* unused)
//{
//	/* We are expecting this to handle SIGCHLD and only when it
//	 * exited. We have the following available
//	 * info.{si_pid,si_uid,si_status,si_utime,si_stime,si_code}
//	 */
//
//	//check the child exited
//	if(info->si_code != CLD_EXITED)
//	{
//		cerr << "Warning: The SIGCHLD handler received information about a child that hasn't exited" << endl;
//		return;
//	}
//}

SolverManager::SolverManager(const std::string& _inputFile, double _timeout) :
solvers(), pidToSolverMap(), inputFile(_inputFile), empty(""), fdToSolverMap(), largestFileDescriptor(0)
{
	//set timeout
	double intPart;
	modf(_timeout,&intPart);

	originalTimeout.tv_sec = timeout.tv_sec = static_cast<time_t>(intPart);
	originalTimeout.tv_nsec = timeout.tv_nsec = 0;

	if(verbose && _timeout != 0)
		cerr << "SolverManager: Using timeout of " << timeout.tv_sec << " second(s)." << endl;

	//Allow the signal handler access to us (the assumption is that only one of us exists)
	//instance=this;

}

SolverManager::~SolverManager()
{
	string solverName("");
	//Try to delete all the solvers
	for(map<pid_t,Solver*>::iterator i = pidToSolverMap.begin(); i != pidToSolverMap.end(); ++i)
	{
		solverName=i->second->toString();

		//Delete the solver. This should kill the solver even if it's still running for some reason
		delete i->second;
		i->second=NULL;

		//Try to reap the child. We don't want any zombies lying around!!
		if(i->first != 0)
		{
			if(verbose) cerr << "Reaping child PID:" << i->first << " (" << solverName << ")" << endl;
			waitpid(i->first,NULL,0);
		}
	}
}

void SolverManager::addSolver(const std::string& name,
		const std::string& cmdLineArgs, bool inputOnStdin)
{
	Solver* s=NULL;
	try
	{
		s=new Solver(name,cmdLineArgs,inputFile, inputOnStdin);
		solvers.push_back(s);

		if(! fdToSolverMap.insert( make_pair(s->getReadFileDescriptor(),s)).second)
			cerr << "Warning: Failed to record file descriptor -> solver mapping" << endl;

		if(verbose)
			cerr << "SolverManager: Added solver \"" << name << "\"" << endl;
	}
	catch(exception& e)
	{
		cerr << "Failed to allocate memory for solver " << name << " : " << e.what() << endl;
		exit(1);
	}
}

void SolverManager::addSolver(const std::string& name, bool inputOnStdin)
{
	addSolver(name,empty, inputOnStdin);
}

bool SolverManager::invokeSolvers()
{
	if(getNumberOfSolvers() == 0)
	{
		cerr << "SolverManager::invokeSolvers : There are no solvers to invoke." << endl;
		return false;
	}

	/* Loop over the solvers. For each solver fork the current process and
	 * execute the solver's code
	 */
	for(vector<Solver*>::iterator s = solvers.begin(); s!= solvers.end(); ++s)
	{
		fflush(stdout);
		fflush(stderr);
		pid_t pid = fork();

		if(pid < 0)
		{
			cerr << "SolverManager::invokeSolvers() : Failed to fork!" << endl;
			return false;
		}
		if(pid == 0)
		{
			//Child code
			(*s)->exec();
		}
		else
		{
			//parent code

			//Add the pid and solver to the map
			if(! pidToSolverMap.insert(std::make_pair(pid,*s)).second )
			{
				cerr << "SolverManager::invokeSolvers() : Failed toSolverManager::invokeSolvers() associate solver " << (*s)->toString() <<
						"with PID:" << pid << endl;
				return false;
			}

			(*s)->setPID(pid);
		}
	}

	//Parent code (wait for solvers)

	//record the start time
	if(clock_gettime(CLOCK_MONOTONIC,&startTime) == -1)
		cerr << "WARNING: Failed to record start time!" << endl;



	Solver* solverOfInterest=NULL;
	int numberOfReadySolvers=0;
	int numberOfUsableSolvers=solvers.size();
	int status=0;

	while(numberOfUsableSolvers!=0)
	{
		setupFileDescriptorSet();

		//Now wait for a solver to return.
		if(timeoutEnabled())
		{
			numberOfReadySolvers = pselect(largestFileDescriptor +1,&lookingToRead,NULL,NULL,&timeout,NULL);
		}
		else
		{
			numberOfReadySolvers = pselect(largestFileDescriptor +1,&lookingToRead,NULL,NULL,NULL,NULL);
		}

		if(numberOfReadySolvers==0)
		{
			//Timeout expired!
			cerr << "Timeout expired!" << endl;
			return false;
		}

		if(numberOfReadySolvers == -1)
		{
			switch(errno)
			{
				case EBADF:
					cerr << "Bad file descriptor in set given to pselect()" << endl;
					return false;
				case EINTR:
					//Unexpected signal
					cerr << "Received unexpected signal while waiting in pselect()" << endl;
					return false;

				case EINVAL:
					//Invalid parameters
					cerr << "Invalid parameters given to pselect()" << endl;
					return false;

				default:
					cerr << "Something went wrong waiting for solver via pselect()" << endl;
					return false;
			}
		}

		solverOfInterest = getSolverFromFileDescriptorSet();
		if(solverOfInterest==NULL)
		{
			cerr << "Error: Couldn't find solver from its file descriptor." << endl;
			return false;
		}


		if(verbose) cerr << "Solver:" << solverOfInterest->toString() << " returned. Checking result..." << endl;

		//remove that solver from the file descriptor map.
		removeSolverFromFileDescriptorSet(solverOfInterest);

		switch(solverOfInterest->getResult())
		{
			case Solver::SAT:
				if(verbose) cerr << "Result: sat" << endl;
				solverOfInterest->dumpResult();
				return true;
			case Solver::UNSAT:
				if(verbose) cerr << "Result: unsat" << endl;
				solverOfInterest->dumpResult();
				return true;

			case Solver::UNKNOWN:
				if(verbose) cerr << "Result: unknown" << endl << "Trying another solver..." << endl;
				//Try another solver
				adjustRemainingTime();
				numberOfUsableSolvers--;
				continue;

			case Solver::ERROR:
				cerr << "Result: Solver (" << solverOfInterest->toString() <<
						") failed." << endl << "Trying another solver..." << endl;

				//Try another solver
				adjustRemainingTime();
				numberOfUsableSolvers--;
				continue;

			default:
				return false;
		}

	}

	cerr << "SolverManager::invokeSolvers() : Ran out of usable solvers!" << endl;
	return false;
}

size_t SolverManager::getNumberOfSolvers()
{
	return solvers.size();
}

bool SolverManager::timeoutEnabled() {
	return (originalTimeout.tv_sec != 0);
}


void SolverManager::adjustRemainingTime()
{
	timespec current;
	if(clock_gettime(CLOCK_MONOTONIC,&current) == -1)
	{
		cerr << "Failed to determine current time." << endl;
		return;
	}

	//calculate elapsed time since start
	time_t elapsedTime= current.tv_sec - startTime.tv_sec;

	if(elapsedTime >= originalTimeout.tv_sec)
		timeout.tv_sec=0;
	else
		timeout.tv_sec = originalTimeout.tv_sec - elapsedTime;

	if(verbose && timeoutEnabled()) cerr << "Remaining time:" << timeout.tv_sec << " second(s)." << endl;
}

void SolverManager::setupFileDescriptorSet()
{
	//set no file descriptors
	FD_ZERO(&lookingToRead);
	largestFileDescriptor=0;

	//Add the file descriptors currently in the map.
	for(map<int,Solver*>::const_iterator i= fdToSolverMap.begin(); i!= fdToSolverMap.end(); ++i)
	{
		//Try to record the largest file descriptor
		if(i->first > largestFileDescriptor) largestFileDescriptor=i->first;

		FD_SET(i->first,&lookingToRead);
	}
}

Solver* SolverManager::getSolverFromFileDescriptorSet()
{
	//Loop through known solvers using the first found solver
	for(vector<Solver*>::const_iterator i=solvers.begin(); i!= solvers.end(); ++i)
	{
		if(FD_ISSET((*i)->getReadFileDescriptor(),&lookingToRead))
			return *i;
	}

	//We didn't find anything.
	return NULL;
}

void SolverManager::removeSolverFromFileDescriptorSet(Solver* s)
{
	for(map<int,Solver*>::iterator i= fdToSolverMap.begin(); i!= fdToSolverMap.end(); ++i)
	{
		if(i->second == s)
		{
			//remove this solver
			fdToSolverMap.erase(i);
			return;
		}
	}
}
