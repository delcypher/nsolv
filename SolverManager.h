#ifndef SOLVERMANAGER_H_
#define SOLVERMANAGER_H_

#include <string>
#include <vector>
#include <map>
#include "Solver.h"
#include <unistd.h>
#include <time.h>
#include <queue>
#include <sys/select.h>

class SolverManager
{
	public:
		SolverManager(const std::string& _inputFile, double _timeOut);
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


};


#endif /* SOLVERMANAGER_H_ */
