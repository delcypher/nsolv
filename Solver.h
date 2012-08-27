#ifndef SOLVER_H_
#define SOLVER_H_

#include <string>
#include <vector>
#include <unistd.h>

class Solver
{
	public:

		enum Result
		{
			SAT,
			UNSAT,
			UNKNOWN,
			ERROR
		};

		//_name is executable path
		//_cmdOptions is a string with space seperated options (empty for no cmd line options)
		Solver(const std::string& _name, const std::string& _cmdOptions, const std::string& _inputFile,
				bool _inputOnStdin);

		//Triggering destructor will kill solver
		~Solver();

		//can only be called once. Should be called just after fork() in parent.
		bool setPID(pid_t p);


		//call from parent when it is known that child finished
		Result getResult();

		//Dump the output from the solver to stdout.
		void dumpResult();

		//Only to be called within child. Will replace current process with solver program.
		void exec();

		int getReadFileDescriptor();

		const std::string& toString();

		static const char* resultToString(Solver::Result r);

	private:
		std::string name;
		std::vector< std::string > cmdOptions;
		std::string inputFile;//Only used if inputOnStdout is true

		//fd[0] is for parent to read from, fd[1] is for child to write to
		int fd[2]; //for use with half-duplex pipe

		pid_t pid;
		const char** argv;

		//Raw byte buffer used to read the first bits of data coming from the child
		unsigned char buffer[7];

		bool inputOnStdin;

		bool resultAlreadyRead;

		void kill();

		void setupArguments(const std::string& _cmdOptions, const std::string& inputFile);

		bool bufferMatch(const char cstring[]);
};


#endif /* SOLVER_H_ */
