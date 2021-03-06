/*
    Copyright (c) Dan Liew 2012

    This file is part of NSolv.

    NSolv is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    NSolv is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with NSolv.  If not, see <http://www.gnu.org/licenses/>
 */
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
#include <fcntl.h>
#include <sys/stat.h>
#include <sstream>
using namespace std;


SolverManager::SolverManager(const std::string& _inputFile, double _timeout, bool _loggingMode ) :
solvers(), pidToSolverMap(), inputFile(_inputFile), empty(""), fdToSolverMap(), largestFileDescriptor(0),
loggingMode(_loggingMode)
{
	//set timeout
	double intPart;
	modf(_timeout,&intPart);

	originalTimeout.tv_sec = timeout.tv_sec = static_cast<time_t>(intPart);
	originalTimeout.tv_nsec = timeout.tv_nsec = 0;

	if(verbose && _timeout != 0)
		cerr << "SolverManager: Using timeout of " << timeout.tv_sec << " second(s)." << endl;

	if(loggingMode)
	{
		if(verbose) cerr << "SolverManager: Using logging mode. Log file is " << loggingPath << endl;

		//Open the file for output and append to previous logging data
		loggingFile.open(loggingPath.c_str(), ios_base::out | ios_base::app);

		//set precision for use with times
		loggingFile.setf(ios::fixed,ios::floatfield);
		loggingFile.precision(9); //show nanosecond precision.

		if(! loggingFile.is_open())
		{
			cerr << "Error : Could not open log file." << endl;
			exit(1);
		}
		else
			loggingFile << "#Start" << endl;

	}

	if(verbose && !loggingMode)
		cerr << "SolverManager: Using performance mode" << endl;

	//Setup up semaphore
	solverSynchronisingSemaphore=NULL;
	stringstream s;
	s << "/nsolv-sem-" << time(NULL); //pick a unique name for the semaphore
	solverSyncName=s.str();
	solverSynchronisingSemaphore=sem_open(solverSyncName.c_str(),
			O_CREAT | //Create a new semaphore if it doesn't already exist
			O_EXCL | //Only allow exclusive access (i.e. with O_CREAT means that we can only create a new semaphore)
			O_CLOEXEC, //Close semaphore on exec() call
			S_IRWXU,
			0U);

	if(solverSynchronisingSemaphore==SEM_FAILED)
	{
		cerr << "SolverManager: Failed to create semaphore for synchronisation" << endl;
		perror(NULL);
	}
	else
		if(verbose) cerr << "SolverManger: Created named semaphore \"" << solverSyncName << "\"" << endl;


}

SolverManager::~SolverManager()
{
	//clean up the semaphore

	if(sem_close(solverSynchronisingSemaphore)!=0)
	{
		cerr << "SolverManager: Failed to close semaphore" << endl;
		perror(NULL);
	}
	else
		if(verbose) cerr << "SolverManger: Closing semaphore \"" << solverSyncName << "\"" << endl;

	//We assume that we are the parent process and so only the parent performs the unlink
	if(sem_unlink(solverSyncName.c_str())!=0)
	{
		cerr << "SolverManager: Failed to unlink semaphore" << endl;
		perror(NULL);
	}
	else
		if(verbose) cerr << "SolverManger: Unlinking semaphore \"" << solverSyncName << "\"" << endl;


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

	//close log
	if(loggingMode) { loggingFile << endl; loggingFile.close();}
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

	if(loggingMode) {listSolversToLog(); printSolverHeaderToLog();}

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
			//In child

			if(verbose) cerr << "SolverManager: Solver \"" << (*s)->toString() << "\" blocking..." << endl;

			/* We will now block (assuming our semaphores is initialised to zero)
			 * until the NSolv parent process lets us go.
			 */
			if(sem_wait(solverSynchronisingSemaphore) !=0)
			{
				perror("Waiting for semaphore failed:");
			}
			if(verbose) cerr << "SolverManager: Solver \"" << (*s)->toString() << "\" unblocked..." << endl;

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

	/* (Parent). All the solvers have now been created. They should all be blocked on our semaphore.
	 * We'll now release the semaphores in the hope that all the solvers will get a fair (depends on
	 * your OS's scheduler) start.
	 */
	for(int numberOfSolvers=0; numberOfSolvers < solvers.size(); numberOfSolvers++)
		sem_post(solverSynchronisingSemaphore);

	//record the start time
	if(clock_gettime(CLOCK_MONOTONIC,&startTime) == -1)
		cerr << "WARNING: Failed to record start time!" << endl;



	Solver* solverOfInterest=NULL;
	int numberOfReadySolvers=0;
	int numberOfUsableSolvers=solvers.size();
	int status=0;

	Solver* winningSolver=NULL;
	Solver::Result solverResult=Solver::ERROR;

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
			if(loggingMode) printUnfinishedSolversToLog();
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

		solverResult=solverOfInterest->getResult();
		switch(solverResult)
		{
			case Solver::SAT:

				if(verbose) cerr << "Result: sat" << endl;

				if(winningSolver==NULL)
				{

					winningSolver=solverOfInterest;//Record the solver that won so we can print its output later.

					if(loggingMode)
						loggingFile << "#First solver to finish " << solverOfInterest->toString() << endl;
				}

				if(!loggingMode)
				{
					//We don't want to let any other solvers run
					numberOfUsableSolvers=0;
					continue;
				}
				else
				{
					//Log output
					printSolverAnswerToLog(solverResult,solverOfInterest->toString());

					//Try the other solvers.
					adjustRemainingTime();
					numberOfUsableSolvers--;
					continue;
				}

			case Solver::UNSAT:

				if(verbose) cerr << "Result: unsat" << endl;

				if(winningSolver==NULL)
				{
						winningSolver=solverOfInterest;//Record the winning solver so we can output its output later.

						if(loggingMode)
							loggingFile << "#First solver to finish " << solverOfInterest->toString() << endl;
				}


				if(!loggingMode)
				{
					//We don't want to let any other solvers run
					numberOfUsableSolvers=0;
					continue;
				}
				else
				{
					//Log output
					printSolverAnswerToLog(solverResult,solverOfInterest->toString());

					//Try the other solvers.
					adjustRemainingTime();
					numberOfUsableSolvers--;
					continue;
				}


			case Solver::UNKNOWN:
				if(verbose) cerr << "Result: unknown" << endl << "Trying another solver..." << endl;

				if(loggingMode) printSolverAnswerToLog(solverResult,solverOfInterest->toString());
				//Try another solver
				adjustRemainingTime();
				numberOfUsableSolvers--;
				continue;

			case Solver::ERROR:
				cerr << "Result: Solver (" << solverOfInterest->toString() <<
						") failed." << endl << "Trying another solver..." << endl;

				if(loggingMode) printSolverAnswerToLog(solverResult,solverOfInterest->toString());

				//Try another solver
				adjustRemainingTime();
				numberOfUsableSolvers--;
				continue;

			default:
				return false;
		}

	}

	if(winningSolver==NULL)
	{
		cerr << "SolverManager::invokeSolvers() : Ran out of usable solvers!" << endl;
		return false;
	}
	else
	{
		/* kill all other solvers if possible.
		 * For some reason if we don't do this calling dumpResult() on the winning solver
		 * blocks. I don't know why, it shouldn't happen because the file descriptor of each solver
		 * should be separate from each other.
		 */

		for(vector<Solver*>::iterator i=solvers.begin(); i!= solvers.end(); ++i)
		{
			if(*i != winningSolver)
				(*i)->kill();
		}

		//print the output of the winning solver
		winningSolver->dumpResult();
		return true;
	}

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
	timespec elapsedTime= subtract(current,startTime);

	if(elapsedTime >= originalTimeout)
		timeout.tv_sec= timeout.tv_nsec =0;
	else
		timeout= subtract(originalTimeout,elapsedTime);

	if(verbose && timeoutEnabled()) cerr << "Remaining time:" << toDouble(timeout) << " second(s)." << endl;
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

void SolverManager::listSolversToLog()
{
	if(!loggingFile.good())
		return;

	loggingFile << "# " << solvers.size() << " solvers: ";

	for(vector<Solver*>::const_iterator i=solvers.begin(); i!= solvers.end(); ++i)
	{
		loggingFile << (*i)->toString() << "," ;
	}

	loggingFile << endl;
}

void SolverManager::printSolverHeaderToLog()
{
	if(!loggingFile.good())
		return;

	loggingFile << "# [Solver name ] [ time (seconds)] [answer]" << endl;
}

void SolverManager::printSolverAnswerToLog(Solver::Result result,
		const std::string& name)
{
	if(!loggingFile.good())
		return;

	timespec current;
	if(clock_gettime(CLOCK_MONOTONIC,&current) == -1)
	{
		cerr << "Failed to determine current time." << endl;
		return;
	}

	//calculate elapsed time since start
	timespec elapsedTime = subtract(current,startTime);

	loggingFile << name << " " << toDouble(elapsedTime) << " " << Solver::resultToString(result) << endl;


}

void SolverManager::printUnfinishedSolversToLog()
{
	if(!loggingFile.good())
		return;

	timespec current;
	if(clock_gettime(CLOCK_MONOTONIC,&current) == -1)
	{
		cerr << "Failed to determine current time." << endl;
		return;
	}

	//calculate elapsed time since start
	timespec elapsedTime = subtract(current,startTime);

	for(map<int,Solver*>::const_iterator i= fdToSolverMap.begin() ; i!= fdToSolverMap.end(); ++i)
	{
		loggingFile << i->second->toString() << " " << toDouble(elapsedTime) << " timeout" << endl;
	}
}

struct timespec subtract(struct timespec a, struct timespec b)
{
	/* Based on by Alex Measday's ts_util function from his General purpose library.
	 * http://www.geonius.com/software/libgpl/ts_util.html
	 */
	struct timespec result;

	/* Handle the case where we would calculate a negative time.
	 * We just return 0.
	 */
	if( (a.tv_sec < b.tv_sec) ||
		((a.tv_sec == b.tv_sec) && (a.tv_nsec < b.tv_nsec) )
	  )
	{
		result.tv_sec = result.tv_sec =0;
		return result;
	}

	//the result will be positive.
	result.tv_sec = a.tv_sec - b.tv_sec;

	//check if we need to borrow from the seconds.
	if( a.tv_nsec < b.tv_nsec)
	{
		result.tv_sec -=1; //borrow a second
		result.tv_nsec = 1000000000L + a.tv_nsec - b.tv_nsec;
	}
	else
		result.tv_nsec = a.tv_nsec - b.tv_nsec;

	return result;
}

double toDouble(struct timespec t)
{
	double value = t.tv_sec;
	value += (static_cast<double>(t.tv_nsec))/1E9;
	return value;
}

bool operator==(struct timespec a, struct timespec b)
{
	if(a.tv_sec == b.tv_sec && a.tv_nsec == b.tv_nsec)
		return true;
	else
		return false;
}

bool operator>(struct timespec a, struct timespec b)
{
	if(a.tv_sec > b.tv_sec)
		return true;

	if( (a.tv_sec == b.tv_sec) && (a.tv_nsec > b.tv_nsec) )
		return true;

	return false;
}

bool operator>=(struct timespec a, struct timespec b) { return ( a==b || a>b);}
