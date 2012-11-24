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
#ifndef SOLVERMANAGER_H_
#define SOLVERMANAGER_H_

#include <fstream>
#include <string>
#include <vector>
#include <map>
#include "Solver.h"
#include <unistd.h>
#include <time.h>
#include <queue>
#include <sys/select.h>
#include <semaphore.h>

class SolverManager
{
	public:
		SolverManager(const std::string& _inputFile, double _timeOut, bool _loggingMode);
		~SolverManager();
		void addSolver(const std::string& name, const std::string& cmdLineArgs, bool inputOnStdin);
		void addSolver(const std::string& name, bool inputOnStdin);
		bool invokeSolvers();

		size_t getNumberOfSolvers();

	private:
		std::vector<Solver*> solvers;
		std::map<pid_t,Solver*> pidToSolverMap;
		std::string inputFile;
		const std::string empty;

		timespec timeout;
		timespec startTime;
		timespec originalTimeout;

		std::map<int,Solver*> fdToSolverMap;

		fd_set lookingToRead;
		int largestFileDescriptor;

		bool loggingMode;
		std::ofstream loggingFile;

		sem_t* solverSynchronisingSemaphore;
		std::string solverSyncName;

		bool timeoutEnabled();


		/*Adjust timeout so that is OriginalTimeout - (currentTime - startTime)
		 * This is needed because if a solver finishes and it has a useless answer we should
		 * wait for the next available solver but only for the remaining time left from what the
		 * user originally asked for.
		 */
		void adjustRemainingTime();

		//Configures "lookingToRead" to be set up for the solvers in "fdToSolverMap"
		void setupFileDescriptorSet();

		Solver* getSolverFromFileDescriptorSet();

		void removeSolverFromFileDescriptorSet(Solver* s);

		void listSolversToLog();

		void printSolverHeaderToLog();

		void printSolverAnswerToLog(Solver::Result result, const std::string& name);

		void printUnfinishedSolversToLog();

};

//helper function a -b
struct timespec subtract(struct timespec a, struct timespec b);
double toDouble(struct timespec t);

bool operator==(struct timespec a, struct timespec b);
bool operator>(struct timespec a, struct timespec b);
bool operator>=(struct timespec a, struct timespec b);

#endif /* SOLVERMANAGER_H_ */
