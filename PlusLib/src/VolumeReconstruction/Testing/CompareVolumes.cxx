/*=Plus=header=begin======================================================
  Program: Plus
  Copyright (c) Laboratory for Percutaneous Surgery. All rights reserved.
  See License.txt for details.
=========================================================Plus=header=end*/

/**
* This program creates a vtkPolyData model that represents tracked ultrasound
* image slices in their tracked positions.
* It can be used to debug geometry problems in volume reconstruction.
* 
*/

#include "PlusConfigure.h"
#include "vtksys/CommandLineArguments.hxx"

#include <fstream>
#include <iostream>
#include <time.h>

#include "vtkDataSetReader.h"
#include "vtkDataSetWriter.h"
#include "vtkImageData.h"
#include "vtkImageClip.h"
//#include "vtkMetaImageWriter.h"
//#include "vtkImageMathematics.h"
//#include "vtkImageLogic.h"
//#include "vtkImageAccumulator.h"

#include "vtkCompareVolumes.h"


int main( int argc, char** argv )
{
  bool printHelp(false);
  std::string inputGTFileName;
  std::string inputGTAlphaFileName;
  std::string inputTestingFileName;
  std::string inputSliceAlphaFileName;
  std::string outputStatsFileName;
  std::vector<int> centerV;
  std::vector<int> sizeV;
  int center[3];
  int size[3];

  int verboseLevel = vtkPlusLogger::LOG_LEVEL_DEFAULT;

  vtksys::CommandLineArguments args;
  args.Initialize(argc, argv);

  args.AddArgument("--input-ground-truth-image", vtksys::CommandLineArguments::EQUAL_ARGUMENT, &inputGTFileName, "The ground truth volume being compared against");
  args.AddArgument("--input-ground-truth-alpha", vtksys::CommandLineArguments::EQUAL_ARGUMENT, &inputGTAlphaFileName, "The ground truth volume's alpha component");
  args.AddArgument("--input-testing-image", vtksys::CommandLineArguments::EQUAL_ARGUMENT, &inputTestingFileName, "The testing image to compare to the ground truth");
  args.AddArgument("--input-slices-alpha", vtksys::CommandLineArguments::EQUAL_ARGUMENT, &inputSliceAlphaFileName, "The alpha component for when the slices are pasted into the volume, without hole filling");
  args.AddArgument("--output-stats-file", vtksys::CommandLineArguments::EQUAL_ARGUMENT, &outputStatsFileName, "The file to dump the statistics for the comparison");
  args.AddArgument("--roi-center", vtksys::CommandLineArguments::MULTI_ARGUMENT, &centerV, "The point at the center of the region of interest");
  args.AddArgument("--roi-size", vtksys::CommandLineArguments::MULTI_ARGUMENT, &sizeV, "The size around the center point to consider");
  args.AddArgument("--verbose", vtksys::CommandLineArguments::EQUAL_ARGUMENT, &verboseLevel, "Verbose level (1=error only, 2=warning, 3=info, 4=debug, 5=trace)");
  args.AddArgument("--help", vtksys::CommandLineArguments::NO_ARGUMENT, &printHelp, "Print this help.");

  // examples:
  //--roi-center 5 6 7
  //--roi-size 4 5 8

  if ( !args.Parse() )
  {
    std::cerr << "Problem parsing arguments" << std::endl;
    std::cout << "Help: " << args.GetHelp() << std::endl;
    exit(EXIT_FAILURE);
  }

  if ( printHelp ) 
  {
    std::cout << "Help: " << args.GetHelp() << std::endl;
    exit(EXIT_SUCCESS); 
  }

  /************************************************************/

	vtkPlusLogger::Instance()->SetLogLevel(verboseLevel);
  
  // record the start time for data recording, see http://www.cplusplus.com/reference/clibrary/ctime/localtime/
  time_t rawtime;
  struct tm* timeInfo;
  time(&rawtime);
  timeInfo = localtime(&rawtime);
  char timeAndDate[60];
  strftime(timeAndDate,60,"%Y %m %d %H:%M",timeInfo);

  // Check file names
  if ( inputGTFileName.empty() || inputGTAlphaFileName.empty() || inputTestingFileName.empty() || inputSliceAlphaFileName.empty() )
  {
    LOG_ERROR("input-ground-truth-image, input-ground-truth-alpha, input-testing-image, input-slices-alpha, and output-stats-file are required arguments!");
    LOG_ERROR("Help: " << args.GetHelp());
    exit(EXIT_FAILURE);
  }

  bool useWholeExtent((centerV.size() == 0)?true:false);

  if (!useWholeExtent)
  {
    // parse and check center
    if (centerV.size() == 3)
    {
      center[0] = centerV[0];
      center[1] = centerV[1];
      center[2] = centerV[2];
    }
    else
    {
      LOG_ERROR("Center needs to be at least 3 values (X,Y,Z)");
      exit(EXIT_FAILURE);
    }

    if (center[0] < 0 || center[1] < 0 || center[2] < 0) 
    {
      LOG_ERROR("Center must consist of positive integers");
      exit(EXIT_FAILURE);
    }

    // parse and check size
    if (sizeV.size() == 3) 
    {
      size[0] = sizeV[0];
      size[1] = sizeV[1];
      size[2] = sizeV[2];
    } 
    else if (sizeV.size() == 1) 
    {
      size[0] = size[1] = size[2] = sizeV[0];
    }
    else
    {
      LOG_ERROR("Size needs to be either 3 values (X,Y,Z), or 1 value for X = Y = Z");
      exit(EXIT_FAILURE);
    }

    if (size[0] <= 0 || size[1] <= 0 || size[2] <= 0) 
    {
      LOG_ERROR("Size must consist of positive integers");
      exit(EXIT_FAILURE);
    }
  }

  // read inputs
  vtkSmartPointer<vtkImageData> groundTruth = vtkSmartPointer<vtkImageData>::New();
  vtkSmartPointer<vtkImageData> groundTruthAlpha = vtkSmartPointer<vtkImageData>::New();
  vtkSmartPointer<vtkImageData> testingImage = vtkSmartPointer<vtkImageData>::New();
  vtkSmartPointer<vtkImageData> slicesAlpha = vtkSmartPointer<vtkImageData>::New();

  // read in the volumes
  LOG_INFO("Reading input ground truth image: " << inputGTFileName );
  vtkSmartPointer<vtkDataSetReader> reader = vtkSmartPointer<vtkDataSetReader>::New();
  reader->SetFileName(inputGTFileName.c_str());
  reader->Update();
  groundTruth = vtkImageData::SafeDownCast(reader->GetOutput());

  LOG_INFO("Reading input ground truth alpha: " << inputGTAlphaFileName );
  vtkSmartPointer<vtkDataSetReader> reader2 = vtkSmartPointer<vtkDataSetReader>::New();
  reader2->SetFileName(inputGTAlphaFileName.c_str());
  reader2->Update();
  groundTruthAlpha = vtkImageData::SafeDownCast(reader2->GetOutput());

  LOG_INFO("Reading input testing image: " << inputTestingFileName );
  vtkSmartPointer<vtkDataSetReader> reader3 = vtkSmartPointer<vtkDataSetReader>::New();
  reader3->SetFileName(inputTestingFileName.c_str());
  reader3->Update();
  testingImage = vtkImageData::SafeDownCast(reader3->GetOutput());

  LOG_INFO("Reading input slices alpha: " << inputSliceAlphaFileName );
  vtkSmartPointer<vtkDataSetReader> reader4 = vtkSmartPointer<vtkDataSetReader>::New();
  reader4->SetFileName(inputSliceAlphaFileName.c_str());
  reader4->Update();
  slicesAlpha = vtkImageData::SafeDownCast(reader4->GetOutput());

  // check to make sure extents match
  int extentGT[6];
  groundTruth->GetExtent(extentGT[0],extentGT[1],extentGT[2],extentGT[3],extentGT[4],extentGT[5]);
  int extentGTAlpha[6];
  groundTruthAlpha->GetExtent(extentGTAlpha[0],extentGTAlpha[1],extentGTAlpha[2],extentGTAlpha[3],extentGTAlpha[4],extentGTAlpha[5]);
  int extentTest[6];
  testingImage->GetExtent(extentTest[0],extentTest[1],extentTest[2],extentTest[3],extentTest[4],extentTest[5]);
  int extentSlicesAlpha[6];
  slicesAlpha->GetExtent(extentSlicesAlpha[0],extentSlicesAlpha[1],extentSlicesAlpha[2],extentSlicesAlpha[3],extentSlicesAlpha[4],extentSlicesAlpha[5]);
  bool match(true);
  for (int i = 0; i < 6; i++)
    if (extentGT[i] != extentGTAlpha[i] || extentGT[i] != extentSlicesAlpha[i] || extentGT[i] != extentSlicesAlpha[i])
      match = false;
  if (!match) {
    LOG_ERROR("Image sizes do not match!");
    exit(EXIT_FAILURE);
  }

  // calculate and check new extents
  int updatedExtent[6];
  updatedExtent[0] = center[0]-size[0];
  updatedExtent[1] = center[0]+size[0];
  updatedExtent[2] = center[1]-size[1];
  updatedExtent[3] = center[1]+size[1];
  updatedExtent[4] = center[2]-size[2];
  updatedExtent[5] = center[2]+size[2];

  if (updatedExtent[0] < extentGT[0] || updatedExtent[1] >= extentGT[1] ||
      updatedExtent[2] < extentGT[2] || updatedExtent[3] >= extentGT[3] ||
      updatedExtent[4] < extentGT[4] || updatedExtent[5] >= extentGT[5]) {
        LOG_ERROR("Region of interest contains data outside the original volume! Extents are: " << updatedExtent[0] << " " << updatedExtent[1] << " " << updatedExtent[2] << " " << updatedExtent[3] << " " << updatedExtent[4] << " " << updatedExtent[5] << "\n" << "Original extent is: " << extentGT[0] << " " << extentGT[1] << " " << extentGT[2] << " " << extentGT[3] << " " << extentGT[4] << " " << extentGT[5]);
        exit(EXIT_FAILURE);
  }

  // crop the image to the ROI
  vtkSmartPointer<vtkImageData> groundTruthCropped = vtkSmartPointer<vtkImageData>::New();
  vtkSmartPointer<vtkImageData> groundTruthAlphaCropped = vtkSmartPointer<vtkImageData>::New();
  vtkSmartPointer<vtkImageData> testingImageCropped = vtkSmartPointer<vtkImageData>::New();
  vtkSmartPointer<vtkImageData> slicesAlphaCropped = vtkSmartPointer<vtkImageData>::New();
  if (!useWholeExtent) {
    vtkSmartPointer<vtkImageClip> clipGT = vtkSmartPointer<vtkImageClip>::New();
    clipGT->SetInput(groundTruth);
    clipGT->SetClipData(1);
    clipGT->SetOutputWholeExtent(updatedExtent);
    clipGT->Update();
    groundTruthCropped = clipGT->GetOutput();
    
    vtkSmartPointer<vtkImageClip> clipGTAlpha = vtkSmartPointer<vtkImageClip>::New();
    clipGTAlpha->SetInput(groundTruthAlpha);
    clipGTAlpha->SetClipData(1);
    clipGTAlpha->SetOutputWholeExtent(updatedExtent);
    clipGTAlpha->Update();
    groundTruthAlphaCropped = clipGTAlpha->GetOutput();

    vtkSmartPointer<vtkImageClip> clipTest = vtkSmartPointer<vtkImageClip>::New();
    clipTest->SetInput(testingImage);
    clipTest->SetClipData(1);
    clipTest->SetOutputWholeExtent(updatedExtent);
    clipTest->Update();
    testingImageCropped = clipTest->GetOutput();
    
    vtkSmartPointer<vtkImageClip> clipSlicesAlpha = vtkSmartPointer<vtkImageClip>::New();
    clipSlicesAlpha->SetInput(slicesAlpha);
    clipSlicesAlpha->SetClipData(1);
    clipSlicesAlpha->SetOutputWholeExtent(updatedExtent);
    clipSlicesAlpha->Update();
    slicesAlphaCropped = clipSlicesAlpha->GetOutput();
  }
  else
  {
    groundTruthCropped = groundTruth;
    groundTruthAlphaCropped = groundTruthAlpha;
    testingImageCropped = testingImage;
    slicesAlphaCropped = slicesAlpha;
  }

  // calculate the histogram for the difference image
  vtkSmartPointer<vtkCompareVolumes> histogramGenerator = vtkSmartPointer<vtkCompareVolumes>::New();
  histogramGenerator->SetInputGT(groundTruthCropped);
  histogramGenerator->SetInputGTAlpha(groundTruthAlphaCropped);
  histogramGenerator->SetInputTest(testingImageCropped);
  histogramGenerator->SetInputSliceAlpha(slicesAlphaCropped);
  histogramGenerator->Update();

  // write data to a CSV
  if (!outputStatsFileName.empty()) {
    int* AbsHistogram = histogramGenerator->GetAbsoluteHistogramPtr();
    int* TruHistogram = histogramGenerator->GetTrueHistogramPtr();
    std::ifstream testingFile(outputStatsFileName.c_str());
    std::ofstream outputStatsFile; // for use once we establish whether or not the file exists
    if (testingFile.is_open())
    { // if the file exists already, just output what we want
      testingFile.close(); // no reading, must open for writing now
      outputStatsFile.open(outputStatsFileName.c_str(),std::ios::out | std::ios::app);
      outputStatsFile << timeAndDate << ","
                   << inputTestingFileName << ","
                   << histogramGenerator->GetNumberOfHoles() << "," 
                   << histogramGenerator->GetTrueMaximum() << ","
                   << histogramGenerator->GetTrueMinimum() << ","
                   << histogramGenerator->GetTrueMedian() << ","
                   << histogramGenerator->GetTrueMean() << ","
                   << histogramGenerator->GetTrueStdev() << ","
                   << histogramGenerator->GetTrue5thPercentile() << ","
                   << histogramGenerator->GetTrue95thPercentile()<< "," 
                   << histogramGenerator->GetAbsoluteMaximum() << ","
                   << histogramGenerator->GetAbsoluteMinimum() << ","
                   << histogramGenerator->GetAbsoluteMedian() << ","
                   << histogramGenerator->GetAbsoluteMean() << ","
                   << histogramGenerator->GetAbsoluteStdev() << ","
                   << histogramGenerator->GetAbsolute5thPercentile() << ","
                   << histogramGenerator->GetAbsolute95thPercentile();
      for (int i = 0; i < 511; i++)
        outputStatsFile << "," << TruHistogram[i];
      for (int i = 0; i < 256; i++)
        outputStatsFile << "," << AbsHistogram[i];
      outputStatsFile << std::endl;
    }
    else
    { // need to create the file and write to it. First give it a header row.
      testingFile.close(); // no reading, must open for writing now
      outputStatsFile.open(outputStatsFileName.c_str());
      outputStatsFile << "Time,Dataset,Number of Holes,True Maximum Error,True Minimum Error,True Median Error,True Mean Error,True Standard Deviation,True 5th Percentile,True 95th Percentile,Absolute Maximum Error,Absolute Minimum Error,Absolute Median Error,Absolute Mean Error,Absolute Standard Deviation,Absolute 5th Percentile,Absolute 95th Percentile";
      for (int i = 0; i < 511; i++) {
        outputStatsFile << "," << (i - 255);
      }
      for (int i = 0; i < 256; i++) {
        outputStatsFile << "," << i;
      }
      outputStatsFile << std::endl;
      outputStatsFile << timeAndDate << ","
                   << inputTestingFileName << ","
                   << histogramGenerator->GetNumberOfHoles() << "," 
                   << histogramGenerator->GetTrueMaximum() << ","
                   << histogramGenerator->GetTrueMinimum() << ","
                   << histogramGenerator->GetTrueMedian() << ","
                   << histogramGenerator->GetTrueMean() << ","
                   << histogramGenerator->GetTrueStdev() << ","
                   << histogramGenerator->GetTrue5thPercentile() << ","
                   << histogramGenerator->GetTrue95thPercentile()<< "," 
                   << histogramGenerator->GetAbsoluteMaximum() << ","
                   << histogramGenerator->GetAbsoluteMinimum() << ","
                   << histogramGenerator->GetAbsoluteMedian() << ","
                   << histogramGenerator->GetAbsoluteMean() << ","
                   << histogramGenerator->GetAbsoluteStdev() << ","
                   << histogramGenerator->GetAbsolute5thPercentile() << ","
                   << histogramGenerator->GetAbsolute95thPercentile();
      for (int i = 0; i < 511; i++)
        outputStatsFile << "," << TruHistogram[i];
      for (int i = 0; i < 256; i++)
        outputStatsFile << "," << AbsHistogram[i];
      outputStatsFile << std::endl;
    }
  }

  return EXIT_SUCCESS;
}