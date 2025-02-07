/** TRACCC library, part of the ACTS project (R&D line)
 *
 * (c) 2021-2023 CERN for the benefit of the ACTS project
 *
 * Mozilla Public License Version 2.0
 */

// Project include(s).
#include "traccc/clusterization/clusterization_algorithm.hpp"
#include "traccc/clusterization/spacepoint_formation.hpp"
#include "traccc/cuda/clusterization/clusterization_algorithm.hpp"
#include "traccc/cuda/seeding/seeding_algorithm.hpp"
#include "traccc/cuda/seeding/track_params_estimation.hpp"
#include "traccc/cuda/utils/stream.hpp"
#include "traccc/efficiency/seeding_performance_writer.hpp"
#include "traccc/io/read_cells.hpp"
#include "traccc/io/read_digitization_config.hpp"
#include "traccc/io/read_geometry.hpp"
#include "traccc/options/common_options.hpp"
#include "traccc/options/detector_input_options.hpp"
#include "traccc/options/full_tracking_input_options.hpp"
#include "traccc/options/handle_argument_errors.hpp"
#include "traccc/performance/collection_comparator.hpp"
#include "traccc/performance/container_comparator.hpp"
#include "traccc/performance/timer.hpp"
#include "traccc/seeding/seeding_algorithm.hpp"
#include "traccc/seeding/track_params_estimation.hpp"

// Acts include(s).
#include "ActsExamples/GenericDetector/GenericDetector.hpp"

#include "Acts/Geometry/ILayerBuilder.hpp"
#include "Acts/Geometry/TrackingGeometry.hpp"
#include "ActsExamples/GenericDetector/BuildGenericDetector.hpp"
#include "ActsExamples/GenericDetector/GenericDetectorElement.hpp"
#include "ActsExamples/GenericDetector/ProtoLayerCreatorT.hpp"

#include "Acts/Definitions/Direction.hpp"
#include "Acts/EventData/MultiTrajectory.hpp"
#include "Acts/EventData/TrackContainer.hpp"
#include "Acts/EventData/TrackStatePropMask.hpp"
#include "Acts/EventData/VectorMultiTrajectory.hpp"
#include "Acts/EventData/VectorTrackContainer.hpp"
#include "Acts/Propagator/EigenStepper.hpp"
#include "Acts/Propagator/Navigator.hpp"
#include "Acts/Propagator/Propagator.hpp"
#include "Acts/TrackFinding/CombinatorialKalmanFilter.hpp"
#include "Acts/TrackFitting/GainMatrixSmoother.hpp"
#include "Acts/TrackFitting/GainMatrixUpdater.hpp"
#include "Acts/Utilities/Logger.hpp"
#include "ActsExamples/EventData/Track.hpp"
#include "ActsExamples/TrackFinding/TrackFindingAlgorithm.hpp"

#include "Acts/Definitions/Algebra.hpp"
#include "Acts/EventData/ProxyAccessor.hpp"
#include "Acts/Surfaces/PerigeeSurface.hpp"
#include "Acts/Surfaces/Surface.hpp"
#include "Acts/TrackFitting/KalmanFitter.hpp"
#include "Acts/Utilities/Delegate.hpp"
#include "ActsExamples/EventData/Measurement.hpp"
#include "ActsExamples/EventData/MeasurementCalibration.hpp"
#include "ActsExamples/Framework/AlgorithmContext.hpp"
#include "ActsExamples/Framework/ProcessCode.hpp"

// VecMem include(s).
#include <vecmem/memory/cuda/device_memory_resource.hpp>
#include <vecmem/memory/cuda/host_memory_resource.hpp>
#include <vecmem/memory/host_memory_resource.hpp>
#include <vecmem/utils/cuda/async_copy.hpp>
#include <vecmem/utils/cuda/copy.hpp>

// System include(s).
#include <exception>
#include <iomanip>
#include <iostream>

#include <algorithm>
#include <map>
#include <memory>
#include <utility>
#include <vector>

#include <cmath>
#include <functional>
#include <optional>
#include <ostream>
#include <stdexcept>
#include <system_error>


namespace po = boost::program_options;

namespace {

using Updater = Acts::GainMatrixUpdater;
using Smoother = Acts::GainMatrixSmoother;

using Stepper = Acts::EigenStepper<>;
using Navigator = Acts::Navigator;
using Propagator = Acts::Propagator<Stepper, Navigator>;
using CKF = Acts::CombinatorialKalmanFilter<Propagator, Acts::VectorMultiTrajectory>;

using TrackContainer =
    Acts::TrackContainer<Acts::VectorTrackContainer,
                         Acts::VectorMultiTrajectory, std::shared_ptr>;

int seq_run(const traccc::full_tracking_input_config& i_cfg,
            const traccc::common_options& common_opts,
            const traccc::detector_input_options& det_opts, bool run_cpu) {

    // Read the surface transforms
    auto surface_transforms = traccc::io::read_geometry(det_opts.detector_file);

    // Read the digitization configuration file
    auto digi_cfg =
        traccc::io::read_digitization_config(i_cfg.digitization_config_file);

    // Output stats
    uint64_t n_cells = 0;
    uint64_t n_modules = 0;
    // uint64_t n_clusters = 0;
    uint64_t n_measurements = 0;
    uint64_t n_spacepoints = 0;
    uint64_t n_spacepoints_cuda = 0;
    uint64_t n_seeds = 0;
    uint64_t n_seeds_cuda = 0;

    // Configs
    traccc::seedfinder_config finder_config;
    traccc::spacepoint_grid_config grid_config(finder_config);
    traccc::seedfilter_config filter_config;

    // Memory resources used by the application.
    vecmem::host_memory_resource host_mr;
    vecmem::cuda::host_memory_resource cuda_host_mr;
    vecmem::cuda::device_memory_resource device_mr;
    traccc::memory_resource mr{device_mr, &cuda_host_mr};

    traccc::clusterization_algorithm ca(host_mr);
    traccc::spacepoint_formation sf(host_mr);
    traccc::seeding_algorithm sa(finder_config, grid_config, filter_config,
                                 host_mr);
    traccc::track_params_estimation tp(host_mr);

    traccc::cuda::stream stream;

    // vecmem::cuda::copy copy;
    vecmem::cuda::async_copy copy{stream.cudaStream()};

    traccc::cuda::clusterization_algorithm ca_cuda(
        mr, copy, stream, common_opts.target_cells_per_partition);
    traccc::cuda::seeding_algorithm sa_cuda(finder_config, grid_config,
                                            filter_config, mr, copy, stream);
    traccc::cuda::track_params_estimation tp_cuda(mr, copy, stream);

    // performance writer
    traccc::seeding_performance_writer sd_performance_writer(
        traccc::seeding_performance_writer::config{});
    // if (common_opts.check_performance) {
    //     sd_performance_writer.add_cache("CPU");
    //     sd_performance_writer.add_cache("CUDA");
    // }

    traccc::performance::timing_info elapsedTimes;

    // Instantiate GenericDetector as part of ACTS setup


///////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    
    //Learn how to set up inputs for setting up the GenericDet - basically read configs

    // const Config& cfg,
    // std::shared_ptr<const Acts::IMaterialDecorator> mdecorator)
    // -> std::pair<TrackingGeometryPtr, ContextDecorators> {
    DetectorElement::ContextType nominalContext;
    // Return the generic detector
    TrackingGeometryPtr gGeometry =
      ActsExamples::Generic::buildDetector<DetectorElement>(
          nominalContext, detectorStore, cfg.buildLevel, std::move(mdecorator),
          cfg.buildProto, cfg.surfaceLogLevel, cfg.layerLogLevel,
          cfg.volumeLogLevel);
    ContextDecorators gContextDecorators = {};
    // return the pair of geometry and empty decorators
    auto GenericDetector =  std::make_pair<TrackingGeometryPtr, ContextDecorators>(
      std::move(gGeometry), std::move(gContextDecorators));

    // Set the CombinatorialKalmanFilter options
    const ActsExamples::AlgorithmContext& ctx

    ActsExamples::TrackFindingAlgorithm::TrackFinderOptions options(
      ctx.geoContext, ctx.magFieldContext, ctx.calibContext, slAccessorDelegate,
      extensions, pOptions, pSurface.get());
    options.smoothingTargetSurfaceStrategy =
    Acts::CombinatorialKalmanFilterTargetSurfaceStrategy::first;

    // Initializing variables required to execute track finding
    auto trackContainer = std::make_shared<Acts::VectorTrackContainer>();
    auto trackStateContainer = std::make_shared<Acts::VectorMultiTrajectory>();

    auto trackContainerTemp = std::make_shared<Acts::VectorTrackContainer>();
    auto trackStateContainerTemp =
      std::make_shared<Acts::VectorMultiTrajectory>();

    TrackContainer tracks(trackContainer, trackStateContainer);
    TrackContainer tracksTemp(trackContainerTemp, trackStateContainerTemp);

    tracks.addColumn<unsigned int>("trackGroup");
    tracksTemp.addColumn<unsigned int>("trackGroup");
    Acts::ProxyAccessor<unsigned int> seedNumber("trackGroup");

    unsigned int nSeed = 0;

    // Instantiate Track Finding function

    Stepper stepper(std::move(magneticField));
    Navigator::Config cfg{std::move(trackingGeometry)};
    cfg.resolvePassive = false;
    cfg.resolveMaterial = true;
    cfg.resolveSensitive = true;
    Navigator navigator(cfg, logger.cloneWithSuffix("Navigator"));
    Propagator propagator(std::move(stepper), std::move(navigator),
                          logger.cloneWithSuffix("Propagator"));
    CKF trackFinder(std::move(propagator), logger.cloneWithSuffix("Finder"));

    // build the track finder functions. owns the track finder object.
    auto tf = std::make_shared<TrackFinderFunctionImpl>(std::move(trackFinder));

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////    

    // Loop over events
    for (unsigned int event = common_opts.skip;
         event < common_opts.events + common_opts.skip; ++event) {

        // Instantiate host containers/collections
        traccc::io::cell_reader_output read_out_per_event(mr.host);
        traccc::clusterization_algorithm::output_type measurements_per_event;
        traccc::spacepoint_formation::output_type spacepoints_per_event;
        traccc::seeding_algorithm::output_type seeds;
        traccc::track_params_estimation::output_type params;

        // Instantiate cuda containers/collections
        traccc::spacepoint_collection_types::buffer spacepoints_cuda_buffer(
            0, *mr.host);
        traccc::seed_collection_types::buffer seeds_cuda_buffer(0, *mr.host);
        traccc::bound_track_parameters_collection_types::buffer
            params_cuda_buffer(0, *mr.host);

        {
            traccc::performance::timer t("Container instantiation  (cpu)",
                                         elapsedTimes);
            // Instantiate host containers/collections
            traccc::io::cell_reader_output read_out_per_event(mr.host);
            traccc::clusterization_algorithm::output_type measurements_per_event;
            traccc::spacepoint_formation::output_type spacepoints_per_event;
            traccc::seeding_algorithm::output_type seeds;
            traccc::track_params_estimation::output_type params;
        }  // stop measuring instantiation timer

        {
            traccc::performance::timer t("Container instantiation  (cuda)",
                                         elapsedTimes);
            // Instantiate cuda containers/collections
            traccc::spacepoint_collection_types::buffer spacepoints_cuda_buffer(
                0, *mr.host);
            traccc::seed_collection_types::buffer seeds_cuda_buffer(0, *mr.host);
            traccc::bound_track_parameters_collection_types::buffer
                params_cuda_buffer(0, *mr.host);
        }  // stop measuring instantiation timer

        {
            traccc::performance::timer wall_t("Wall time", elapsedTimes);

            {
                traccc::performance::timer t("File reading  (cpu)",
                                             elapsedTimes);
                // Read the cells from the relevant event file into host memory.
                traccc::io::read_cells(read_out_per_event, event,
                                       common_opts.input_directory,
                                       common_opts.input_data_format,
                                       &surface_transforms, &digi_cfg);
            }  // stop measuring file reading timer

            const traccc::cell_collection_types::host& cells_per_event =
                read_out_per_event.cells;
            const traccc::cell_module_collection_types::host&
                modules_per_event = read_out_per_event.modules;

            /*-----------------------------
                Clusterization and Spacepoint Creation (cuda)
            -----------------------------*/
            // Create device copy of input collections
            traccc::cell_collection_types::buffer cells_buffer(
                cells_per_event.size(), mr.main);
            copy(vecmem::get_data(cells_per_event), cells_buffer);
            traccc::cell_module_collection_types::buffer modules_buffer(
                modules_per_event.size(), mr.main);
            copy(vecmem::get_data(modules_per_event), modules_buffer);

            {
                traccc::performance::timer t("Host to device  (Cells, modules)",
                                             elapsedTimes);
                traccc::cell_collection_types::buffer cells_buffer(
                    cells_per_event.size(), mr.main);
                copy(vecmem::get_data(cells_per_event), cells_buffer);
                traccc::cell_module_collection_types::buffer modules_buffer(
                    modules_per_event.size(), mr.main);
                copy(vecmem::get_data(modules_per_event), modules_buffer);
            }  // stop measuring H2D timer

            {
                traccc::performance::timer t("Clusterization (cuda)",
                                             elapsedTimes);
                // Reconstruct it into spacepoints on the device.
                spacepoints_cuda_buffer =
                    ca_cuda(cells_buffer, modules_buffer).first;
                stream.synchronize();
            }  // stop measuring clusterization cuda timer

            if (run_cpu) {

                /*-----------------------------
                    Clusterization (cpu)
                -----------------------------*/

                {
                    traccc::performance::timer t("Clusterization  (cpu)",
                                                 elapsedTimes);
                    measurements_per_event =
                        ca(cells_per_event, modules_per_event);
                }  // stop measuring clusterization cpu timer

                /*---------------------------------
                    Spacepoint formation (cpu)
                ---------------------------------*/

                {
                    traccc::performance::timer t("Spacepoint formation  (cpu)",
                                                 elapsedTimes);
                    spacepoints_per_event =
                        sf(measurements_per_event, modules_per_event);
                }  // stop measuring spacepoint formation cpu timer
            }

            /*----------------------------
                Seeding algorithm
            ----------------------------*/

            // CUDA

            {
                traccc::performance::timer t("Seeding (cuda)", elapsedTimes);
                seeds_cuda_buffer = sa_cuda(spacepoints_cuda_buffer);
                stream.synchronize();
            }  // stop measuring seeding cuda timer

            // CPU

            if (run_cpu) {
                traccc::performance::timer t("Seeding  (cpu)", elapsedTimes);
                seeds = sa(spacepoints_per_event);
            }  // stop measuring seeding cpu timer

            /*----------------------------
            Track params estimation
            ----------------------------*/

            // CUDA

            {
                traccc::performance::timer t("Track params (cuda)",
                                             elapsedTimes);
                params_cuda_buffer =
                    tp_cuda(spacepoints_cuda_buffer, seeds_cuda_buffer,
                            {0.f, 0.f, finder_config.bFieldInZ});
                stream.synchronize();
            }  // stop measuring track params timer

            // CPU

            if (run_cpu) {
                traccc::performance::timer t("Track params  (cpu)",
                                             elapsedTimes);
                params = tp(spacepoints_per_event, seeds,
                            {0.f, 0.f, finder_config.bFieldInZ});
            }  // stop measuring track params cpu timer

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////

            if (run_cpu) {
                for (std::size_t iseed = 0; iseed < params.size(); ++iseed) {
                    // Clear trackContainerTemp and trackStateContainerTemp
                    tracksTemp.clear();

                    auto result =
                        (*tf.findTracks)(params.at(iseed), options, tracksTemp);
                    m_nTotalSeeds++;
                    nSeed++;

                    if (!resul;t.ok()) {
                    m_nFailedSeeds++;
                    std::cout << "Track finding failed for seed " << iseed << " with error"
                                                                << result.error();
                    continue;
                    }
                    auto& tracksForSeed = result.value();
                    for (auto& track : tracksForSeed) {
                      // Set the seed number, this number decrease by 1 since the seed number
                      // has already been updated
                      seedNumber(track) = nSeed - 1;
                      if (!m_trackSelector.has_value() ||
                          m_trackSelector->isValidTrack(track)) {
                        auto destProxy = tracks.getTrack(tracks.addTrack());
                        destProxy.copyFrom(track, true);  // make sure we copy track states!
                        }
                    }
                }
            }

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////

        }  // Stop measuring wall time

        /*----------------------------------
          compare cpu and cuda result
          ----------------------------------*/

        traccc::spacepoint_collection_types::host spacepoints_per_event_cuda;
        traccc::seed_collection_types::host seeds_cuda;
        traccc::bound_track_parameters_collection_types::host params_cuda;

        if (run_cpu || common_opts.check_performance) {

            copy(spacepoints_cuda_buffer, spacepoints_per_event_cuda)->wait();
            copy(seeds_cuda_buffer, seeds_cuda)->wait();
            copy(params_cuda_buffer, params_cuda)->wait();
        }

        {
            traccc::performance::timer t("Device to host  (spacepoints)",
                                         elapsedTimes);
            copy(spacepoints_cuda_buffer, spacepoints_per_event_cuda);
        }

        {
            traccc::performance::timer t("Device to host  (seeds)",
                                         elapsedTimes);
            copy(seeds_cuda_buffer, seeds_cuda);
        }

        {
            traccc::performance::timer t("Device to host  (track params)",
                                         elapsedTimes);
            copy(params_cuda_buffer, params_cuda);
        }

        if (run_cpu) {
            // Show which event we are currently presenting the results for.
            std::cout << "===>>> Event " << event << " <<<===" << std::endl;

            // Compare the spacepoints made on the host and on the device.
            traccc::collection_comparator<traccc::spacepoint>
                compare_spacepoints{"spacepoints"};
            compare_spacepoints(vecmem::get_data(spacepoints_per_event),
                                vecmem::get_data(spacepoints_per_event_cuda));

            // Compare the seeds made on the host and on the device
            traccc::collection_comparator<traccc::seed> compare_seeds{
                "seeds", traccc::details::comparator_factory<traccc::seed>{
                             vecmem::get_data(spacepoints_per_event),
                             vecmem::get_data(spacepoints_per_event_cuda)}};
            compare_seeds(vecmem::get_data(seeds),
                          vecmem::get_data(seeds_cuda));

            // Compare the track parameters made on the host and on the device.
            traccc::collection_comparator<traccc::bound_track_parameters>
                compare_track_parameters{"track parameters"};
            compare_track_parameters(vecmem::get_data(params),
                                     vecmem::get_data(params_cuda));
        }
        /// Statistics
        n_modules += read_out_per_event.modules.size();
        n_cells += read_out_per_event.cells.size();
        n_measurements += measurements_per_event.size();
        n_spacepoints += spacepoints_per_event.size();
        n_seeds += seeds.size();
        n_spacepoints_cuda += spacepoints_per_event_cuda.size();
        n_seeds_cuda += seeds_cuda.size();

        if (common_opts.check_performance) {

            traccc::event_map evt_map(
                event, det_opts.detector_file, i_cfg.digitization_config_file,
                common_opts.input_directory, common_opts.input_directory,
                common_opts.input_directory, host_mr);
            sd_performance_writer.write(
                vecmem::get_data(seeds_cuda),
                vecmem::get_data(spacepoints_per_event_cuda), evt_map);

            // if (run_cpu) {
            //     sd_performance_writer.write(
            //         "CPU", vecmem::get_data(seeds),
            //         vecmem::get_data(spacepoints_per_event), evt_map);
            // }
        }
    }

    if (common_opts.check_performance) {
        sd_performance_writer.finalize();
    }

    std::cout << "==> Statistics ... " << std::endl;
    std::cout << "- read    " << n_cells << " cells from " << n_modules
              << " modules" << std::endl;
    std::cout << "- created (cpu)  " << n_measurements << " measurements     "
              << std::endl;
    std::cout << "- created (cpu)  " << n_spacepoints << " spacepoints     "
              << std::endl;
    std::cout << "- created (cuda) " << n_spacepoints_cuda
              << " spacepoints     " << std::endl;

    std::cout << "- created  (cpu) " << n_seeds << " seeds" << std::endl;
    std::cout << "- created (cuda) " << n_seeds_cuda << " seeds" << std::endl;
    std::cout << "==>Elapsed times...\n" << elapsedTimes << std::endl;

    return 0;

} // namespace

}

// The main routine
//
int main(int argc, char* argv[]) {
    // Set up the program options
    po::options_description desc("Allowed options");

    // Add options
    desc.add_options()("help,h", "Give some help with the program's options");
    traccc::common_options common_opts(desc);
    traccc::detector_input_options det_opts(desc);
    traccc::full_tracking_input_config full_tracking_input_cfg(desc);
    desc.add_options()("run_cpu", po::value<bool>()->default_value(false),
                       "run cpu tracking as well");

    po::variables_map vm;
    po::store(po::parse_command_line(argc, argv, desc), vm);

    // Check errors
    traccc::handle_argument_errors(vm, desc);

    // Read options
    common_opts.read(vm);
    det_opts.read(vm);
    full_tracking_input_cfg.read(vm);
    auto run_cpu = vm["run_cpu"].as<bool>();

    std::cout << "Running " << argv[0] << " "
              << full_tracking_input_cfg.detector_file << " "
              << common_opts.input_directory << " " << common_opts.events
              << std::endl;

    return seq_run(full_tracking_input_cfg, common_opts, det_opts, run_cpu);
}
