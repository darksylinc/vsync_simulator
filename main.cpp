
#include <assert.h>
#include <getopt.h>
#include <math.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <algorithm>
#include <deque>

// Change these parameters, recompile and run to see
// the results

// PARAMETER BEGIN
static const size_t kMaxNumBuffers = 16u;

static size_t kNumBuffers = 3u;
static size_t kNumSwapchains = 4u;

static size_t kVBlank = 16u;

static size_t kCpuTime = 7u;
static size_t kGpuTime = 17u;

static size_t kCpuFrameVariance = 2u;
static size_t kGpuFrameVariance = 2u;
// PARAMETER END

/// Returns value in range [0; bound)
static uint32_t bounded_rand( uint32_t bound )
{
	if( bound == 0u )
		return 0u;

	uint32_t threshold = -bound % bound;

	for( ;; )
	{
		uint32_t r = (uint32_t)rand();
		if( r >= threshold )
			return r % bound;
	}
}

static size_t calculateFrameTime( size_t _timeToTake, const size_t variance )
{
	int32_t timeToTake = int32_t( _timeToTake );
	const int32_t randomVariance =
		int32_t( bounded_rand( uint32_t( variance * 2u + 1u ) ) ) - int32_t( variance );

	timeToTake += randomVariance;
	timeToTake = std::max<int32_t>( timeToTake, 0 );
	return (size_t)timeToTake;
}

struct SubmittedToGpuWork
{
	size_t bufferIdx;
	size_t cpuTimeStart;
	size_t timeToTake;
	size_t tickSinceLast;

	size_t getCpuFinishedWork() const { return cpuTimeStart + timeToTake; }
};

struct SubmittedSwapchain
{
	SubmittedToGpuWork cpuSubmission;
	size_t gpuTimeStart;
	size_t timeToTake;
	size_t swapchainIdx;

	size_t getFinishedWork() const { return gpuTimeStart + timeToTake; }
};

static size_t clamp( size_t val, size_t minVal, size_t maxVal )
{
	return std::min( std::max( val, minVal ), maxVal );
}

void calculateStats( std::vector<size_t> &samples, double &outAvg, double &outStdDev )
{
	size_t sumVal = 0u;
	for( const size_t sample : samples )
		sumVal += sample;

	const double avg = (double)sumVal / (double)samples.size();

	double sumDev = 0;
	for( const size_t sample : samples )
		sumDev += ( (double)sample - avg ) * ( (double)sample - avg );

	outAvg = avg;
	outStdDev = std::sqrt( sumDev / (double)samples.size() );
}

int main( int argc, char *argv[] )
{
	SubmittedSwapchain lockedSwapchain;

	size_t cpuTicksBusy = 0u;
	bool bFence[kMaxNumBuffers];
	std::deque<SubmittedToGpuWork> submittedToGpuWork;
	bool bGpuWorking = false;
	SubmittedSwapchain workInProgress;
	std::deque<SubmittedSwapchain> finishedWork;
	std::deque<size_t> availableSwapchains;

	srand( 101 );  // Deterministic output

	size_t numTicks = 1000u;

	printf( "RUN WITH SETTINGS:\n" );
	for( int i = 0; i < argc; ++i )
		printf( "%s ", argv[i] );
	printf( "\n\n" );

	optind = 1;

	while( 1 )
	{
		/* --vblank_interval 16 --buffer_count 2 --swapchain_count 3 --cpu_time 7 --cpu_time_variance 2
		 * --gpu_time 12 --gpu_time_variance 2 --num_ticks 1000
		 */
		int option_index = 0;
		static struct option long_options[] = { { "vblank_interval", required_argument, 0, 0 },
												{ "buffer_count", required_argument, 0, 0 },
												{ "swapchain_count", required_argument, 0, 0 },
												{ "cpu_time", required_argument, 0, 0 },
												{ "cpu_time_variance", required_argument, 0, 0 },
												{ "gpu_time", required_argument, 0, 0 },
												{ "gpu_time_variance", required_argument, 0, 0 },
												{ "num_ticks", required_argument, 0, 0 },
												{ 0, 0, 0, 0 } };

		const int c = getopt_long( argc, argv, "", long_options, &option_index );
		if( c == -1 )
			break;

		if( c == 0 && optarg )
		{
			switch( option_index )
			{
			case 0:
				kVBlank = (size_t)atoi( optarg );
				break;
			case 1:
				kNumBuffers = clamp( (size_t)atoi( optarg ), 1u, kMaxNumBuffers );
				break;
			case 2:
				kNumSwapchains = std::max<size_t>( 2u, (size_t)atoi( optarg ) );
				break;
			case 3:
				kCpuTime = (size_t)atoi( optarg );
				break;
			case 4:
				kCpuFrameVariance = (size_t)atoi( optarg );
				break;
			case 5:
				kGpuTime = (size_t)atoi( optarg );
				break;
			case 6:
				kGpuFrameVariance = (size_t)atoi( optarg );
				break;
			case 7:
				numTicks = (size_t)atoi( optarg );
				break;
			}
		}
	}

	size_t currFrameIdx = 0u;

	lockedSwapchain.cpuSubmission.cpuTimeStart = 0;
	lockedSwapchain.gpuTimeStart = 0;
	lockedSwapchain.swapchainIdx = kNumSwapchains - 1u;
	for( size_t i = 0u; i < kNumBuffers; ++i )
		bFence[i] = true;

	for( size_t i = 0u; i < kNumSwapchains - 1u; ++i )
		availableSwapchains.push_back( i );

	size_t tickStart = 0u;

	size_t worstLag = 0u;
	std::vector<size_t> lagValues;
	std::vector<size_t> mspfValues;
	size_t hitVBlanks = 0u;
	size_t missedVBlanks = 0u;

	for( size_t i = 0u; i < numTicks; ++i )
	{
		if( i != 0u && ( i % kVBlank ) == 0u )
		{
			// Time to present
			bool bBlankHit = false;
			if( !finishedWork.empty() )
			{
				const SubmittedSwapchain &work = finishedWork.front();
				if( i >= work.getFinishedWork() )
				{
					availableSwapchains.push_back( lockedSwapchain.swapchainIdx );
					assert( availableSwapchains.size() <= kNumSwapchains - 1u );
					lockedSwapchain = work;
					finishedWork.pop_front();

					bBlankHit = true;

					const size_t lag = i - lockedSwapchain.cpuSubmission.cpuTimeStart;

					printf(
						"FRAME PRESENTED! t = %i; timeStart = %i; worst_case_lag = %i; mspf = %i; "
						" fps = %.2f\n",
						(int)i, (int)lockedSwapchain.cpuSubmission.cpuTimeStart, (int)lag,
						(int)lockedSwapchain.cpuSubmission.tickSinceLast,
						1000.0f / (float)lockedSwapchain.cpuSubmission.tickSinceLast );

					if( hitVBlanks >= 3u )
					{
						mspfValues.push_back( lockedSwapchain.cpuSubmission.tickSinceLast );
						worstLag = std::max( worstLag, lag );
						lagValues.push_back( lag );
					}
					++hitVBlanks;
				}
			}

			if( !bBlankHit )
			{
				printf( "VBLANK MISSED! t = %i\n", (int)i );
				++missedVBlanks;
			}
		}

		if( bFence[currFrameIdx] && cpuTicksBusy == 0u )
		{
			// Start CPU work
			SubmittedToGpuWork work;
			work.bufferIdx = currFrameIdx;
			work.cpuTimeStart = i;
			work.timeToTake = calculateFrameTime( kCpuTime, kCpuFrameVariance );
			work.tickSinceLast = std::max<size_t>( i - tickStart, 1u );
			tickStart = i;

			cpuTicksBusy = kCpuTime;

			submittedToGpuWork.push_back( work );
			bFence[currFrameIdx] = false;

			assert( submittedToGpuWork.size() <= kNumBuffers );

			currFrameIdx = ( currFrameIdx + 1u ) % kNumBuffers;
		}

		if( cpuTicksBusy > 0u )
			--cpuTicksBusy;

		if( !submittedToGpuWork.empty() && !availableSwapchains.empty() && !bGpuWorking )
		{
			// We can only do one GPU job per tick
			const SubmittedToGpuWork &work = submittedToGpuWork.front();

			if( i >= work.getCpuFinishedWork() )
			{
				// GPU work started.
				SubmittedSwapchain gpuWork;
				gpuWork.swapchainIdx = availableSwapchains.front();
				gpuWork.cpuSubmission = work;
				gpuWork.timeToTake = calculateFrameTime( kGpuTime, kGpuFrameVariance );
				gpuWork.gpuTimeStart = i;

				workInProgress = gpuWork;
				bGpuWorking = true;

				availableSwapchains.pop_front();
				submittedToGpuWork.pop_front();
			}
		}

		if( ( i + 1u ) >= workInProgress.getFinishedWork() && bGpuWorking )
		{
			// Signal CPU it can start using this bufferIdx.
			bFence[workInProgress.cpuSubmission.bufferIdx] = true;

			finishedWork.push_back( workInProgress );
			bGpuWorking = false;
		}
	}

	double avgMspf, mspfStdDev, avgLag, lagStdDev;
	calculateStats( mspfValues, avgMspf, mspfStdDev );
	calculateStats( lagValues, avgLag, lagStdDev );

	printf( "\nSummary:\n" );
	printf( "Total VBLANKs hits = %i; missed = %i\n", (int)hitVBlanks, (int)missedVBlanks );
	printf( "Avg MSPF = %.02fms;\tStd Dev MSPF = %.02fms;\tAvg FPS   = %.02f FPS\n", avgMspf, mspfStdDev,
			1000.0 / avgMspf );
	printf( "Avg Lag  = %.02fms;\tStd Dev Lag  = %.02fms;\tWorst Lag = %ims\n", avgLag, lagStdDev,
			(int)worstLag );
	printf( "==========================================================================\n" );

	return 0;
}
