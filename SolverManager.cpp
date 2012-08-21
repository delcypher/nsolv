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

SolverManager::SolverManager(const std::string& _inputFile, double _timeout) :
solvers(), pidToSolverMap(), inputFile(_inputFile), empty("")
{
	//set timeout
	double intPart;
	modf(_timeout,&intPart);

	originalTimeout.tv_sec = timeout.tv_sec = static_cast<time_t>(intPart);
	originalTimeout.tv_nsec = timeout.tv_nsec = 0;

	if(verbose && _timeout != 0)
		cerr << "SolverManager: Using timeout of " << timeout.tv_sec << " second(s)." << endl;

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
	try
	{
		solvers.push_back(new Solver(name,cmdLineArgs,inputFile, inputOnStdin));

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

	/* We block (the signal isn't lost, it is queued for us by the kernel) SIGCHLD.
	 * This prevents a race-condition where the child exits before we call sigwait() or sigtimedwait()
	 */
	sigset_t signals;
	sigemptyset(&signals);
	sigaddset(&signals,SIGCHLD);

	if(sigprocmask(SIG_BLOCK,&signals,NULL) == -1)
	{
		cerr << "SolverManager::invokeSolvers() : Failed to block SIGCHLD." << endl;
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

			//We inherent the blocking of SIGCHLD. The solver might do its own forking so we should unblock it
			if(sigprocmask(SIG_UNBLOCK,&signals,NULL) == -1)
			{
				cerr << "SolverManager::invokeSolvers() : Warning couldn't unblock SIGCHLD in child process." << endl;
			}


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


	//record the start time
	if(clock_gettime(CLOCK_MONOTONIC,&startTime) == -1)
		cerr << "WARNING: Failed to record start time!" << endl;

	int returnedSignalNumber;
	siginfo_t info;
	memset(&info,0,sizeof(info));
	Solver* solverOfInterest=NULL;
	pid_t returningSolver=0;
	int numberOfUsableSolvers=solvers.size();
	int status=0;
	while(true)
	{
		if(numberOfUsableSolvers ==0)
		{
			cerr << "SolverManager::invokeSolvers() : Ran out of usable solvers!" << endl;
			return false;
		}


		//Now wait for a solver to return.
		if(timeoutEnabled())
		{
			returnedSignalNumber = sigtimedwait(&signals,&info,&timeout);
		}
		else
		{
			returnedSignalNumber = sigwaitinfo(&signals,&info);
		}

		if(returnedSignalNumber == -1)
		{
			switch(errno)
			{
				case EAGAIN:
					//Timeout has occured
					cerr << "Timeout expired!" << endl;
					return false;
				case EINTR:
					//Unexpected signal
					cerr << "Received unexpected signal!" << endl;
					return false;

				case EINVAL:
					//Invalid timeout
					cerr << "sigtimedwait() was given an invalid timeout!" << endl;
					return false;

				default:
					cerr << "Something went wrong waiting for SIGCHLD" << endl;
					return false;
			}
		}

		/* We need to examine the solver that returned (info.si_pid is empty!!)
		 * We seem to need to use waitpid(). There's no guarantee that it's the same solver
		 * that sigtimedwait() or sigwaitinfo() received
		 * but let's hope it is.
		 */
		returningSolver=waitpid(0,&status,0);
		if(returningSolver == -1)
		{
			cerr << "SolverManager::invokeSolvers() : Couldn't get the PID of the solver that returned" << endl;
			return false;
		}

		if(pidToSolverMap.count(returningSolver) != 1)
		{
			cerr << "SolverManager::invokeSolvers() : The returned Solver (" << returningSolver <<
					") didn't have an expected PID." << endl;
			return false;
		}


		solverOfInterest=pidToSolverMap.find(returningSolver)->second;

		if(!WIFEXITED(status))
		{
			//Child didn't exit cleanly!
			cerr << "SolverManager::invokeSolvers() : Solver " << solverOfInterest->toString() <<
					" did not exit cleanly!" << endl;
			adjustRemainingTime();
			//decrement the number of available solvers
			numberOfUsableSolvers--;
			continue;
		}

		if(verbose) cerr << "Solver:" << solverOfInterest->toString() << " returned. Checking result..." << endl;

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

	return false; //shouldn't be reachable
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
