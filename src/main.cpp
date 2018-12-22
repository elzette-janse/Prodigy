/**
 * Code adapted from https://github.com/mlpack/models/blob/master/Kaggle/DigitRecognizer/src/DigitRecognizer.cpp
 */

#include <iostream>

#include <mlpack/core.hpp>
#include <mlpack/core/data/split_data.hpp>

#include <mlpack/core/optimizers/sgd/sgd.hpp>
#include <mlpack/core/optimizers/adam/adam_update.hpp>

#include <mlpack/methods/ann/layer/layer.hpp>
#include <mlpack/methods/ann/rnn.hpp>
#include <mlpack/methods/ann/layer/lstm.hpp>
#include <mlpack/methods/ann/loss_functions/kl_divergence.hpp>
#include <mlpack/methods/ann/loss_functions/mean_squared_error.hpp>
#include <mlpack/methods/ann/loss_functions/cross_entropy_error.hpp>
#include <mlpack/prereqs.hpp>

using namespace mlpack;
using namespace mlpack::ann;
using namespace mlpack::optimization;

using namespace arma;
using namespace std;

/**
 * Returns labels bases on predicted probability (or log of probability)
 * of classes.
 * @param predOut matrix contains probabilities (or log of probability) of
 * classes. Each row corresponds to a certain class, each column corresponds
 * to a data point.
 * @return a row vector of data point's classes. The classes starts from 1 to
 * the number of rows in input matrix.
 */
arma::Row<size_t> getLabels(const arma::cube& predOut)
{
    arma::Row<size_t> pred(predOut.n_cols);
    
    // Class of a j-th data point is chosen to be the one with maximum value
    // in j-th column plus 1 (since column's elements are numbered from 0).
    for (size_t j = 0; j < predOut.n_cols; ++j)
    {
        pred(j) = arma::as_scalar(arma::find(
                                             arma::max(predOut.col(j)) == predOut.col(j), 1)) + 1;
    }
    
    return pred;
}

// Prepare input of sequence of notes for LSTM
arma::cube getTrainX(const mat& tempDataset, const int& sequence_length)
{
    const int num_notes = tempDataset.n_rows;	
    const int num_sequences = (num_notes / sequence_length) + 1;
    cube trainX = cube(1, num_sequences, sequence_length);	
    for (unsigned int i = 0; i < num_sequences; i++)
    {
	for (unsigned int j = 0; j < sequence_length; j++)
	{
		trainX.at(1,i,j) = tempDataset.at(i+j,0);
	}
    }
    return trainX;
 }

// Generate array with 1 in the indice of the note present at a time step
mat getCategory(const mat& tempDataset, const int& size_notes, const int& sequence_length)
{
    mat trainY = mat(tempDataset.n_rows - sequence_length, size_notes, fill::zeros);
    for (unsigned int i = sequence_length; i < tempDataset.n_rows; i++)
    {
	int note = tempDataset.at(i,0);
	trainY.at(i-sequence_length, note) = 1;
    }
    return trainY;
}

 /**
 * Returns the accuracy (percentage of correct answers).
 * @param predLabels predicted labels of data points.
 * @param real actual notes (they are double because we usually read them from
 * CSV file that contain many other double values).
 * @return percentage of correct answers.
 */
double accuracy(arma::mat& predLabels, const arma::cube& real)
{
    cout << "predicted" << predLabels << endl;
    cout << "real" << real << endl;
    // Calculating how many predicted notes coincide with actual notes.
    size_t success = 0;
    for (size_t j = 0; j < real.n_cols; j++) {
        if (predLabels(0,j,predLabels.n_slices-1) == std::round(real.at(0,j,0))) {
            ++success;
        }
    }
    
    // Calculating percentage of correctly predicted notes.
    return (double) success / (double)real.n_cols * 100.0;
}

void trainModel(RNN<MeanSquaredError<>>& model,
                const cube& trainX, const mat& trainY)
{
    // The solution is done in several approaches (CYCLES), so each approach
    // uses previous results as starting point and have a different optimizer
    // options (here the step size is different).
    
    // Number of iteration per cycle.
    constexpr int ITERATIONS_PER_CYCLE = 10000;
    
    // Number of cycles.
    constexpr int CYCLES = 50;
    
    // Step size of an optimizer.
    constexpr double STEP_SIZE = 5e-20;
    
    // Number of data points in each iteration of SGD
    constexpr int BATCH_SIZE = 5;
    
    // Setting parameters Stochastic Gradient Descent (SGD) optimizer.
    SGD<AdamUpdate> optimizer(
                              // Step size of the optimizer.
                              STEP_SIZE,
                              // Batch size. Number of data points that are used in each iteration.
                              BATCH_SIZE,
                              // Max number of iterations
                              ITERATIONS_PER_CYCLE,
                              // Tolerance, used as a stopping condition. This small number
                              // means we never stop by this condition and continue to optimize
                              // up to reaching maximum of iterations.
                              1e-8,
    			      false,
    			      // Adam update policy.
    			      AdamUpdate(1e-8, 0.9, 0.999));
    			      
   
    // Cycles for monitoring the process of a solution.
    for (int i = 0; i <= CYCLES; i++)
    {
        // Train neural network. If this is the first iteration, weights are
        // random, using current values as starting point otherwise.
       
       	model.Train(trainX, trainY, optimizer);
       
        // Don't reset optimizer's parameters between cycles.
        optimizer.ResetPolicy() = false;
        
        cube predOut;
        // Getting predictions on training data points.
        model.Predict(trainX, predOut);
        // Calculating accuracy on training data points.
        double trainAccuracy = accuracy(predOut, trainY);       

        cout << i << " - accuracy: train = "<< trainAccuracy << "%," << endl;
        
    }
}

/**
 * Run the neural network model and predict the class for a
 * set of testing example
 */
void predictClass(RNN<MeanSquaredError<>>& model,
                  const std::string datasetName, const int rho)
{
    
    mat tempDataset;
    data::Load(datasetName, tempDataset, true);
    
    cube test = cube(1,tempDataset.n_cols,rho);
    for (unsigned int i = 0; i < tempDataset.n_cols; i++)
    {
	test(0,i,0) = tempDataset.at(0,i);
    }

    
    cube testPredOut;
    // Getting predictions on test data points .
    model.Predict(test, testPredOut);
    // Generating labels for the test dataset.
    //Row<size_t> testPred = getLabels(testPredOut);
    cout << "Saving predicted labels to \"results.csv\" ..." << endl;
    mat testPred(1,testPredOut.n_cols);
    for (unsigned int i = 0; i < testPredOut.n_cols; i++)
    {
	testPred(0,i) = testPredOut.at(0,i,testPredOut.n_slices-1);
    }
    // Saving results into Kaggle compatibe CSV file.
    data::Save("results.csv", testPred); // testPred or test??
    cout << "Results were saved to \"results.csv\"" << endl;
}

int main () {
    
    cout << "Reading data ..." << endl; 
	
    mat tempDataset;
    const int rho = 8;

    data::Load("../utils/training.csv", tempDataset, true); // read data from this csv file, creates arma matrix with loaded data in tempDataset
    
   
    const int size_notes = max(tempDataset.row(0));
    const int sequence_length = 3;
	
    cube trainX = getTrainX(tempDataset, sequence_length);
    mat trainY = getCategory(tempDataset, size_notes, sequence_length);
    cout << trainX << trainY << endl;

    // According to NegativeLogLikelihood output layer of NN, labels should
    // specify class of a data point and be in the interval from 1 to
    // number of classes (in this case from 1 to 10).
    
    // Specifying the NN model. NegativeLogLikelihood is the output layer that
    // is used for classification problem. RandomInitialization means that
    // initial weights in neurons are generated randomly in the interval
    // from -1 to 1.
	
    RNN<MeanSquaredError<> > model(rho);
    model.Add<Linear <> > (trainX.n_rows, rho);
    model.Add<LSTM <> > (rho,512);
    model.Add<Linear <> > (512, 256);
    model.Add<Dropout <> > (0.3);
    model.Add<Linear <> > (256, size_notes);
    //model.Add<SigmoidLayer <> >();
    model.Add<LogSoftMax<> > ();
    	
    cout << "Training ..." << endl;
    trainModel(model, trainX, trainY);
    
    cout << "Predicting ..." << endl;
    std::string datasetName = "../utils/test.csv";
    predictClass(model, datasetName,rho);
    cout << "Finished" << endl;
    
    return 0;
}