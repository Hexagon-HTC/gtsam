/* ----------------------------------------------------------------------------

 * GTSAM Copyright 2010, Georgia Tech Research Corporation,
 * Atlanta, Georgia 30332-0415
 * All Rights Reserved
 * Authors: Frank Dellaert, et al. (see THANKS for the full author list)

 * See LICENSE for the license information

 * -------------------------------------------------------------------------- */

/**
 * @file    testHybridIncremental.cpp
 * @brief   Unit tests for incremental inference
 * @author  Fan Jiang, Varun Agrawal, Frank Dellaert
 * @date    Jan 2021
 */

#include <gtsam/discrete/DiscreteBayesNet.h>
#include <gtsam/discrete/DiscreteDistribution.h>
#include <gtsam/discrete/DiscreteFactorGraph.h>
#include <gtsam/geometry/Pose2.h>
#include <gtsam/hybrid/HybridConditional.h>
#include <gtsam/hybrid/HybridGaussianISAM.h>
#include <gtsam/linear/GaussianBayesNet.h>
#include <gtsam/linear/GaussianFactorGraph.h>
#include <gtsam/nonlinear/PriorFactor.h>
#include <gtsam/sam/BearingRangeFactor.h>

#include <numeric>

#include "Switching.h"

// Include for test suite
#include <CppUnitLite/TestHarness.h>

using namespace std;
using namespace gtsam;
using noiseModel::Isotropic;
using symbol_shorthand::L;
using symbol_shorthand::M;
using symbol_shorthand::W;
using symbol_shorthand::X;
using symbol_shorthand::Y;
using symbol_shorthand::Z;

/* ****************************************************************************/
// Test if we can perform elimination incrementally.
TEST(HybridGaussianElimination, IncrementalElimination) {
  Switching switching(3);
  HybridGaussianISAM isam;
  HybridGaussianFactorGraph graph1;

  // Create initial factor graph
  //  *        *      *
  //  |        |      |
  //  X1  -*-  X2 -*- X3
  //   \*-M1-*/
  graph1.push_back(switching.linearizedFactorGraph.at(0));  // P(X1)
  graph1.push_back(switching.linearizedFactorGraph.at(1));  // P(X1, X2 | M1)
  graph1.push_back(switching.linearizedFactorGraph.at(2));  // P(X2, X3 | M2)
  graph1.push_back(switching.linearizedFactorGraph.at(5));  // P(M1)

  // Run update step
  isam.update(graph1);

  // Check that after update we have 3 hybrid Bayes net nodes:
  // P(X1 | X2, M1) and P(X2, X3 | M1, M2), P(M1, M2)
  EXPECT_LONGS_EQUAL(3, isam.size());
  EXPECT(isam[X(1)]->conditional()->frontals() == KeyVector{X(1)});
  EXPECT(isam[X(1)]->conditional()->parents() == KeyVector({X(2), M(1)}));
  EXPECT(isam[X(2)]->conditional()->frontals() == KeyVector({X(2), X(3)}));
  EXPECT(isam[X(2)]->conditional()->parents() == KeyVector({M(1), M(2)}));

  /********************************************************/
  // New factor graph for incremental update.
  HybridGaussianFactorGraph graph2;

  graph1.push_back(switching.linearizedFactorGraph.at(3));  // P(X2)
  graph2.push_back(switching.linearizedFactorGraph.at(4));  // P(X3)
  graph2.push_back(switching.linearizedFactorGraph.at(6));  // P(M1, M2)

  isam.update(graph2);

  // Check that after the second update we have
  // 1 additional hybrid Bayes net node:
  // P(X2, X3 | M1, M2)
  EXPECT_LONGS_EQUAL(3, isam.size());
  EXPECT(isam[X(3)]->conditional()->frontals() == KeyVector({X(2), X(3)}));
  EXPECT(isam[X(3)]->conditional()->parents() == KeyVector({M(1), M(2)}));
}

/* ****************************************************************************/
// Test if we can incrementally do the inference
TEST(HybridGaussianElimination, IncrementalInference) {
  Switching switching(3);
  HybridGaussianISAM isam;
  HybridGaussianFactorGraph graph1;

  // Create initial factor graph
  //    *        *        *
  //    |        |        |
  //    X1  -*-  X2  -*-  X3
  //         |        |
  //      *-M1 - * - M2
  graph1.push_back(switching.linearizedFactorGraph.at(0));  // P(X1)
  graph1.push_back(switching.linearizedFactorGraph.at(1));  // P(X1, X2 | M1)
  graph1.push_back(switching.linearizedFactorGraph.at(3));  // P(X2)
  graph1.push_back(switching.linearizedFactorGraph.at(5));  // P(M1)

  // Run update step
  isam.update(graph1);

  auto discreteConditional_m1 =
      isam[M(1)]->conditional()->asDiscreteConditional();
  EXPECT(discreteConditional_m1->keys() == KeyVector({M(1)}));

  /********************************************************/
  // New factor graph for incremental update.
  HybridGaussianFactorGraph graph2;

  graph2.push_back(switching.linearizedFactorGraph.at(2));  // P(X2, X3 | M2)
  graph2.push_back(switching.linearizedFactorGraph.at(4));  // P(X3)
  graph2.push_back(switching.linearizedFactorGraph.at(6));  // P(M1, M2)

  isam.update(graph2);

  /********************************************************/
  // Run batch elimination so we can compare results.
  Ordering ordering;
  ordering += X(1);
  ordering += X(2);
  ordering += X(3);

  // Now we calculate the actual factors using full elimination
  HybridBayesTree::shared_ptr expectedHybridBayesTree;
  HybridGaussianFactorGraph::shared_ptr expectedRemainingGraph;
  std::tie(expectedHybridBayesTree, expectedRemainingGraph) =
      switching.linearizedFactorGraph.eliminatePartialMultifrontal(ordering);

  // The densities on X(1) should be the same
  auto x1_conditional =
      dynamic_pointer_cast<GaussianMixture>(isam[X(1)]->conditional()->inner());
  auto actual_x1_conditional = dynamic_pointer_cast<GaussianMixture>(
      (*expectedHybridBayesTree)[X(1)]->conditional()->inner());
  EXPECT(assert_equal(*x1_conditional, *actual_x1_conditional));

  // The densities on X(2) should be the same
  auto x2_conditional =
      dynamic_pointer_cast<GaussianMixture>(isam[X(2)]->conditional()->inner());
  auto actual_x2_conditional = dynamic_pointer_cast<GaussianMixture>(
      (*expectedHybridBayesTree)[X(2)]->conditional()->inner());
  EXPECT(assert_equal(*x2_conditional, *actual_x2_conditional));

  // The densities on X(3) should be the same
  auto x3_conditional =
      dynamic_pointer_cast<GaussianMixture>(isam[X(3)]->conditional()->inner());
  auto actual_x3_conditional = dynamic_pointer_cast<GaussianMixture>(
      (*expectedHybridBayesTree)[X(2)]->conditional()->inner());
  EXPECT(assert_equal(*x3_conditional, *actual_x3_conditional));

  // We only perform manual continuous elimination for 0,0.
  // The other discrete probabilities on M(2) are calculated the same way
  Ordering discrete_ordering;
  discrete_ordering += M(1);
  discrete_ordering += M(2);
  HybridBayesTree::shared_ptr discreteBayesTree =
      expectedRemainingGraph->eliminateMultifrontal(discrete_ordering);

  DiscreteValues m00;
  m00[M(1)] = 0, m00[M(2)] = 0;
  DiscreteConditional decisionTree =
      *(*discreteBayesTree)[M(2)]->conditional()->asDiscreteConditional();
  double m00_prob = decisionTree(m00);

  auto discreteConditional = isam[M(2)]->conditional()->asDiscreteConditional();

  // Test if the probability values are as expected with regression tests.
  DiscreteValues assignment;
  EXPECT(assert_equal(m00_prob, 0.0619233, 1e-5));
  assignment[M(1)] = 0;
  assignment[M(2)] = 0;
  EXPECT(assert_equal(m00_prob, (*discreteConditional)(assignment), 1e-5));
  assignment[M(1)] = 1;
  assignment[M(2)] = 0;
  EXPECT(assert_equal(0.183743, (*discreteConditional)(assignment), 1e-5));
  assignment[M(1)] = 0;
  assignment[M(2)] = 1;
  EXPECT(assert_equal(0.204159, (*discreteConditional)(assignment), 1e-5));
  assignment[M(1)] = 1;
  assignment[M(2)] = 1;
  EXPECT(assert_equal(0.2, (*discreteConditional)(assignment), 1e-5));

  // Check if the clique conditional generated from incremental elimination
  // matches that of batch elimination.
  auto expectedChordal = expectedRemainingGraph->eliminateMultifrontal();
  auto expectedConditional = dynamic_pointer_cast<DecisionTreeFactor>(
      (*expectedChordal)[M(2)]->conditional()->inner());
  auto actualConditional = dynamic_pointer_cast<DecisionTreeFactor>(
      isam[M(2)]->conditional()->inner());
  EXPECT(assert_equal(*actualConditional, *expectedConditional, 1e-6));
}

/* ****************************************************************************/
// Test if we can approximately do the inference
TEST(HybridGaussianElimination, Approx_inference) {
  Switching switching(4);
  HybridGaussianISAM incrementalHybrid;
  HybridGaussianFactorGraph graph1;

  // Add the 3 hybrid factors, x1-x2, x2-x3, x3-x4
  for (size_t i = 1; i < 4; i++) {
    graph1.push_back(switching.linearizedFactorGraph.at(i));
  }

  // Add the Gaussian factors, 1 prior on X(1),
  // 3 measurements on X(2), X(3), X(4)
  graph1.push_back(switching.linearizedFactorGraph.at(0));
  for (size_t i = 4; i <= 7; i++) {
    graph1.push_back(switching.linearizedFactorGraph.at(i));
  }

  // Create ordering.
  Ordering ordering;
  for (size_t j = 1; j <= 4; j++) {
    ordering += X(j);
  }

  // Now we calculate the actual factors using full elimination
  HybridBayesTree::shared_ptr unprunedHybridBayesTree;
  HybridGaussianFactorGraph::shared_ptr unprunedRemainingGraph;
  std::tie(unprunedHybridBayesTree, unprunedRemainingGraph) =
      switching.linearizedFactorGraph.eliminatePartialMultifrontal(ordering);

  size_t maxNrLeaves = 5;
  incrementalHybrid.update(graph1);

  incrementalHybrid.prune(M(3), maxNrLeaves);

  /*
  unpruned factor is:
    Choice(m3)
    0 Choice(m2)
    0 0 Choice(m1)
    0 0 0 Leaf 0.11267528
    0 0 1 Leaf 0.18576102
    0 1 Choice(m1)
    0 1 0 Leaf 0.18754662
    0 1 1 Leaf 0.30623871
    1 Choice(m2)
    1 0 Choice(m1)
    1 0 0 Leaf 0.18576102
    1 0 1 Leaf 0.30622428
    1 1 Choice(m1)
    1 1 0 Leaf 0.30623871
    1 1 1 Leaf  0.5

  pruned factors is:
    Choice(m3)
    0 Choice(m2)
    0 0 Leaf    0
    0 1 Choice(m1)
    0 1 0 Leaf 0.18754662
    0 1 1 Leaf 0.30623871
    1 Choice(m2)
    1 0 Choice(m1)
    1 0 0 Leaf    0
    1 0 1 Leaf 0.30622428
    1 1 Choice(m1)
    1 1 0 Leaf 0.30623871
    1 1 1 Leaf  0.5
  */

  auto discreteConditional_m1 = *dynamic_pointer_cast<DiscreteConditional>(
      incrementalHybrid[M(1)]->conditional()->inner());
  EXPECT(discreteConditional_m1.keys() == KeyVector({M(1), M(2), M(3)}));

  // Get the number of elements which are greater than 0.
  auto count = [](const double &value, int count) {
    return value > 0 ? count + 1 : count;
  };
  // Check that the number of leaves after pruning is 5.
  EXPECT_LONGS_EQUAL(5, discreteConditional_m1.fold(count, 0));

  // Check that the hybrid nodes of the bayes net match those of the pre-pruning
  // bayes net, at the same positions.
  auto &unprunedLastDensity = *dynamic_pointer_cast<GaussianMixture>(
      unprunedHybridBayesTree->clique(X(4))->conditional()->inner());
  auto &lastDensity = *dynamic_pointer_cast<GaussianMixture>(
      incrementalHybrid[X(4)]->conditional()->inner());

  std::vector<std::pair<DiscreteValues, double>> assignments =
      discreteConditional_m1.enumerate();
  // Loop over all assignments and check the pruned components
  for (auto &&av : assignments) {
    const DiscreteValues &assignment = av.first;
    const double value = av.second;

    if (value == 0.0) {
      EXPECT(lastDensity(assignment) == nullptr);
    } else {
      CHECK(lastDensity(assignment));
      EXPECT(assert_equal(*unprunedLastDensity(assignment),
                          *lastDensity(assignment)));
    }
  }
}

/* ****************************************************************************/
// Test approximate inference with an additional pruning step.
TEST(HybridGaussianElimination, Incremental_approximate) {
  Switching switching(5);
  HybridGaussianISAM incrementalHybrid;
  HybridGaussianFactorGraph graph1;

  /***** Run Round 1 *****/
  // Add the 3 hybrid factors, x1-x2, x2-x3, x3-x4
  for (size_t i = 1; i < 4; i++) {
    graph1.push_back(switching.linearizedFactorGraph.at(i));
  }

  // Add the Gaussian factors, 1 prior on X(1),
  // 3 measurements on X(2), X(3), X(4)
  graph1.push_back(switching.linearizedFactorGraph.at(0));
  for (size_t i = 5; i <= 7; i++) {
    graph1.push_back(switching.linearizedFactorGraph.at(i));
  }

  // Run update with pruning
  size_t maxComponents = 5;
  incrementalHybrid.update(graph1);
  incrementalHybrid.prune(M(3), maxComponents);

  // Check if we have a bayes tree with 4 hybrid nodes,
  // each with 2, 4, 8, and 5 (pruned) leaves respetively.
  EXPECT_LONGS_EQUAL(4, incrementalHybrid.size());
  EXPECT_LONGS_EQUAL(
      2, incrementalHybrid[X(1)]->conditional()->asMixture()->nrComponents());
  EXPECT_LONGS_EQUAL(
      4, incrementalHybrid[X(2)]->conditional()->asMixture()->nrComponents());
  EXPECT_LONGS_EQUAL(
      5, incrementalHybrid[X(3)]->conditional()->asMixture()->nrComponents());
  EXPECT_LONGS_EQUAL(
      5, incrementalHybrid[X(4)]->conditional()->asMixture()->nrComponents());

  /***** Run Round 2 *****/
  HybridGaussianFactorGraph graph2;
  graph2.push_back(switching.linearizedFactorGraph.at(4));
  graph2.push_back(switching.linearizedFactorGraph.at(8));

  // Run update with pruning a second time.
  incrementalHybrid.update(graph2);
  incrementalHybrid.prune(M(4), maxComponents);

  // Check if we have a bayes tree with pruned hybrid nodes,
  // with 5 (pruned) leaves.
  CHECK_EQUAL(5, incrementalHybrid.size());
  EXPECT_LONGS_EQUAL(
      5, incrementalHybrid[X(4)]->conditional()->asMixture()->nrComponents());
  EXPECT_LONGS_EQUAL(
      5, incrementalHybrid[X(5)]->conditional()->asMixture()->nrComponents());
}

/* ************************************************************************/
// A GTSAM-only test for running inference on a single-legged robot.
// The leg links are represented by the chain X-Y-Z-W, where X is the base and
// W is the foot.
// We use BetweenFactor<Pose2> as constraints between each of the poses.
TEST(HybridGaussianISAM, NonTrivial) {
  /*************** Run Round 1 ***************/
  HybridNonlinearFactorGraph fg;

  // Add a prior on pose x1 at the origin.
  // A prior factor consists of a mean  and
  // a noise model (covariance matrix)
  Pose2 prior(0.0, 0.0, 0.0);  // prior mean is at origin
  auto priorNoise = noiseModel::Diagonal::Sigmas(
      Vector3(0.3, 0.3, 0.1));  // 30cm std on x,y, 0.1 rad on theta
  fg.emplace_nonlinear<PriorFactor<Pose2>>(X(0), prior, priorNoise);

  // create a noise model for the landmark measurements
  auto poseNoise = noiseModel::Isotropic::Sigma(3, 0.1);

  // We model a robot's single leg as X - Y - Z - W
  // where X is the base link and W is the foot link.

  // Add connecting poses similar to PoseFactors in GTD
  fg.emplace_nonlinear<BetweenFactor<Pose2>>(X(0), Y(0), Pose2(0, 1.0, 0),
                                             poseNoise);
  fg.emplace_nonlinear<BetweenFactor<Pose2>>(Y(0), Z(0), Pose2(0, 1.0, 0),
                                             poseNoise);
  fg.emplace_nonlinear<BetweenFactor<Pose2>>(Z(0), W(0), Pose2(0, 1.0, 0),
                                             poseNoise);

  // Create initial estimate
  Values initial;
  initial.insert(X(0), Pose2(0.0, 0.0, 0.0));
  initial.insert(Y(0), Pose2(0.0, 1.0, 0.0));
  initial.insert(Z(0), Pose2(0.0, 2.0, 0.0));
  initial.insert(W(0), Pose2(0.0, 3.0, 0.0));

  HybridGaussianFactorGraph gfg = fg.linearize(initial);
  fg = HybridNonlinearFactorGraph();

  HybridGaussianISAM inc;

  // Update without pruning
  // The result is a HybridBayesNet with no discrete variables
  // (equivalent to a GaussianBayesNet).
  // Factorization is:
  // `P(X | measurements) = P(W0|Z0) P(Z0|Y0) P(Y0|X0) P(X0)`
  inc.update(gfg);

  /*************** Run Round 2 ***************/
  using PlanarMotionModel = BetweenFactor<Pose2>;

  // Add odometry factor with discrete modes.
  Pose2 odometry(1.0, 0.0, 0.0);
  KeyVector contKeys = {W(0), W(1)};
  auto noise_model = noiseModel::Isotropic::Sigma(3, 1.0);
  auto still = boost::make_shared<PlanarMotionModel>(W(0), W(1), Pose2(0, 0, 0),
                                                     noise_model),
       moving = boost::make_shared<PlanarMotionModel>(W(0), W(1), odometry,
                                                      noise_model);
  std::vector<PlanarMotionModel::shared_ptr> components = {moving, still};
  auto mixtureFactor = boost::make_shared<MixtureFactor>(
      contKeys, DiscreteKeys{gtsam::DiscreteKey(M(1), 2)}, components);
  fg.push_back(mixtureFactor);

  // Add equivalent of ImuFactor
  fg.emplace_nonlinear<BetweenFactor<Pose2>>(X(0), X(1), Pose2(1.0, 0.0, 0),
                                             poseNoise);
  // PoseFactors-like at k=1
  fg.emplace_nonlinear<BetweenFactor<Pose2>>(X(1), Y(1), Pose2(0, 1, 0),
                                             poseNoise);
  fg.emplace_nonlinear<BetweenFactor<Pose2>>(Y(1), Z(1), Pose2(0, 1, 0),
                                             poseNoise);
  fg.emplace_nonlinear<BetweenFactor<Pose2>>(Z(1), W(1), Pose2(-1, 1, 0),
                                             poseNoise);

  initial.insert(X(1), Pose2(1.0, 0.0, 0.0));
  initial.insert(Y(1), Pose2(1.0, 1.0, 0.0));
  initial.insert(Z(1), Pose2(1.0, 2.0, 0.0));
  // The leg link did not move so we set the expected pose accordingly.
  initial.insert(W(1), Pose2(0.0, 3.0, 0.0));

  gfg = fg.linearize(initial);
  fg = HybridNonlinearFactorGraph();

  // Update without pruning
  // The result is a HybridBayesNet with 1 discrete variable M(1).
  // P(X | measurements) = P(W0|Z0, W1, M1) P(Z0|Y0, W1, M1) P(Y0|X0, W1, M1)
  //                       P(X0 | X1, W1, M1) P(W1|Z1, X1, M1) P(Z1|Y1, X1, M1)
  //                       P(Y1 | X1, M1)P(X1 | M1)P(M1)
  // The MHS tree is a 1 level tree for time indices (1,) with 2 leaves.
  inc.update(gfg);

  /*************** Run Round 3 ***************/
  // Add odometry factor with discrete modes.
  contKeys = {W(1), W(2)};
  still = boost::make_shared<PlanarMotionModel>(W(1), W(2), Pose2(0, 0, 0),
                                                noise_model);
  moving =
      boost::make_shared<PlanarMotionModel>(W(1), W(2), odometry, noise_model);
  components = {moving, still};
  mixtureFactor = boost::make_shared<MixtureFactor>(
      contKeys, DiscreteKeys{gtsam::DiscreteKey(M(2), 2)}, components);
  fg.push_back(mixtureFactor);

  // Add equivalent of ImuFactor
  fg.emplace_nonlinear<BetweenFactor<Pose2>>(X(1), X(2), Pose2(1.0, 0.0, 0),
                                             poseNoise);
  // PoseFactors-like at k=1
  fg.emplace_nonlinear<BetweenFactor<Pose2>>(X(2), Y(2), Pose2(0, 1, 0),
                                             poseNoise);
  fg.emplace_nonlinear<BetweenFactor<Pose2>>(Y(2), Z(2), Pose2(0, 1, 0),
                                             poseNoise);
  fg.emplace_nonlinear<BetweenFactor<Pose2>>(Z(2), W(2), Pose2(-2, 1, 0),
                                             poseNoise);

  initial.insert(X(2), Pose2(2.0, 0.0, 0.0));
  initial.insert(Y(2), Pose2(2.0, 1.0, 0.0));
  initial.insert(Z(2), Pose2(2.0, 2.0, 0.0));
  initial.insert(W(2), Pose2(0.0, 3.0, 0.0));

  gfg = fg.linearize(initial);
  fg = HybridNonlinearFactorGraph();

  // Now we prune!
  // P(X | measurements) = P(W0|Z0, W1, M1) P(Z0|Y0, W1, M1) P(Y0|X0, W1, M1)
  //                       P(X0 | X1, W1, M1) P(W1|W2, Z1, X1, M1, M2)
  //                       P(Z1| W2, Y1, X1, M1, M2) P(Y1 | W2, X1, M1, M2)
  //                       P(X1 | W2, X2, M1, M2) P(W2|Z2, X2, M1, M2)
  //                       P(Z2|Y2, X2, M1, M2) P(Y2 | X2, M1, M2)
  //                       P(X2 | M1, M2) P(M1, M2)
  // The MHS at this point should be a 2 level tree on (1, 2).
  // 1 has 2 choices, and 2 has 4 choices.
  inc.update(gfg);
  inc.prune(M(2), 2);

  /*************** Run Round 4 ***************/
  // Add odometry factor with discrete modes.
  contKeys = {W(2), W(3)};
  still = boost::make_shared<PlanarMotionModel>(W(2), W(3), Pose2(0, 0, 0),
                                                noise_model);
  moving =
      boost::make_shared<PlanarMotionModel>(W(2), W(3), odometry, noise_model);
  components = {moving, still};
  mixtureFactor = boost::make_shared<MixtureFactor>(
      contKeys, DiscreteKeys{gtsam::DiscreteKey(M(3), 2)}, components);
  fg.push_back(mixtureFactor);

  // Add equivalent of ImuFactor
  fg.emplace_nonlinear<BetweenFactor<Pose2>>(X(2), X(3), Pose2(1.0, 0.0, 0),
                                             poseNoise);
  // PoseFactors-like at k=3
  fg.emplace_nonlinear<BetweenFactor<Pose2>>(X(3), Y(3), Pose2(0, 1, 0),
                                             poseNoise);
  fg.emplace_nonlinear<BetweenFactor<Pose2>>(Y(3), Z(3), Pose2(0, 1, 0),
                                             poseNoise);
  fg.emplace_nonlinear<BetweenFactor<Pose2>>(Z(3), W(3), Pose2(-3, 1, 0),
                                             poseNoise);

  initial.insert(X(3), Pose2(3.0, 0.0, 0.0));
  initial.insert(Y(3), Pose2(3.0, 1.0, 0.0));
  initial.insert(Z(3), Pose2(3.0, 2.0, 0.0));
  initial.insert(W(3), Pose2(0.0, 3.0, 0.0));

  gfg = fg.linearize(initial);
  fg = HybridNonlinearFactorGraph();

  // Keep pruning!
  inc.update(gfg);
  inc.prune(M(3), 3);

  // The final discrete graph should not be empty since we have eliminated
  // all continuous variables.
  auto discreteTree = inc[M(3)]->conditional()->asDiscreteConditional();
  EXPECT_LONGS_EQUAL(3, discreteTree->size());

  // Test if the optimal discrete mode assignment is (1, 1, 1).
  DiscreteFactorGraph discreteGraph;
  discreteGraph.push_back(discreteTree);
  DiscreteValues optimal_assignment = discreteGraph.optimize();

  DiscreteValues expected_assignment;
  expected_assignment[M(1)] = 1;
  expected_assignment[M(2)] = 1;
  expected_assignment[M(3)] = 1;

  EXPECT(assert_equal(expected_assignment, optimal_assignment));

  // Test if pruning worked correctly by checking that we only have 3 leaves in
  // the last node.
  auto lastConditional = inc[X(3)]->conditional()->asMixture();
  EXPECT_LONGS_EQUAL(3, lastConditional->nrComponents());
}

/* ************************************************************************* */
int main() {
  TestResult tr;
  return TestRegistry::runAllTests(tr);
}
