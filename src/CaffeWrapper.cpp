
#include "CaffeWrapper.h"
#include "caffe/caffe.hpp"

#include "opencv2/imgproc/imgproc.hpp"
#include "opencv2/calib3d/calib3d.hpp"
#include "opencv2/highgui/highgui.hpp"

using namespace CNN;
using namespace caffe;

namespace caffe{
	LayerRegisterer<float> get_g_creator_f_CONVOLUTION();
}

CaffeWrapper::CaffeWrapper(const char *model_path, const char *trained_params_path)
	:
CNNEngine()
{
	Caffe::set_mode(Caffe::CPU);

    model = new Net<float>(model_path, caffe::TEST);

	model->CopyTrainedLayersFrom(trained_params_path);
}

CaffeWrapper::~CaffeWrapper()
{
    delete model;
}

CNNEngine::CNNResult CaffeWrapper::PredictImage(const char *image_filepath)
{
	return PredictImage(image_filepath, UPRIGHT);
}

bool CaffeWrapper::PreprocessAndLoadImage(const char *image_filepath, IMAGE_ORIENTATION orientation)
{
	cv::Mat src = cv::imread(image_filepath);

	if(src.cols == 0 || src.rows == 0) {
		return false;
	}

	if(orientation != UPRIGHT && orientation != ORIENTATION_UNKNOWN)
	{
		cv::Mat dest;
		cv::Mat rot_mat;
		if(orientation == UPSIDE_DOWN)
		{
			rot_mat = cv::getRotationMatrix2D(cv::Point2f(src.cols * 0.5f, src.rows * 0.5f), -180.f, 1.f);
		}
		else if(orientation == ANTI_CLOCKWISE_90)
		{
			rot_mat = cv::getRotationMatrix2D(cv::Point2f(src.cols * 0.5f, src.rows * 0.5f), 90.f, 1.f);
		}
		else if(orientation == CLOCKWISE_90)
		{
			rot_mat = cv::getRotationMatrix2D(cv::Point2f(src.cols * 0.5f, src.rows * 0.5f), -90.f, 1.f);
		}
		cv::warpAffine(src, dest, rot_mat, src.size());
		src = dest;
	}

	cv::Mat dest;

	const boost::shared_ptr<MemoryDataLayer<double> > memory_layer = 
		boost::dynamic_pointer_cast<MemoryDataLayer<double> >(model->layer_by_name("data"));

	// resize the image to 256x256 as image needs to be the same size as the mean image used by the memory layer
	// presumably this should be handled by the transformer.
	int thumb_width = 256;
	int thumb_height = 256;

	// following ImageNet, the image is resized so the shortest side if 256, we then crop out a 256x256 from the
	// centre of the remaining image.
	cv::Size new_size;
	cv::Rect rect;
	if(src.rows > src.cols)
	{
		new_size = cv::Size(thumb_width, src.rows * ((float)thumb_width / (float)src.cols));
		rect.x = 0;
		rect.width = thumb_width;
		rect.height = thumb_height;
		rect.y = (int)(0.5 + (new_size.height - thumb_height) * 0.5f);
	}
	else
	{
		new_size = cv::Size(src.cols * ((float)thumb_height / (float)src.rows), thumb_height);
		rect.y = 0;
		rect.width = thumb_width;
		rect.height = thumb_height;
		rect.x = (int)(0.5 + (new_size.width - thumb_width) * 0.5f);
	}

	// sanity check the resize
	if(new_size.height < thumb_height) {
		new_size.height = thumb_height;
	}
	if(new_size.width < thumb_width) {
		new_size.width = thumb_width;
	}

	cv::resize(src, dest, new_size);
	dest = dest(rect);

	std::vector<cv::Mat> input;
	input.push_back(dest);
	std::vector<int> label;
	label.push_back(0);

	memory_layer->AddMatVector(input, label);

	return true;
}

CNNEngine::CNNResult CaffeWrapper::PredictImage(const char *image_filepath, IMAGE_ORIENTATION orientation)
{
	CNNResult result;

	PreprocessAndLoadImage(image_filepath, orientation);

	// classify the image
	float loss = 0.f;
	const vector<Blob<float>*>& output = model->ForwardPrefilled(&loss);

	// populate the results, store the top N results, store the scores and the string
	for(int n=0; n < CNNResult::NUM_RESULTS; n++)
	{
		result.class_id[n] = -1;
		result.scores[n] = -9.9e9;
	}
	
	for (int i = 0; i < output[1]->count(); ++i) 
	{
		float value = output[1]->cpu_data()[i];
		for(size_t n=0; n < CNNResult::NUM_RESULTS; n++)
		{
			if(value > result.scores[n])
			{
				// move all results down 1
				for(int j=CNNResult::NUM_RESULTS-1; j > n; j--)
				{
					result.class_id[j] = result.class_id[j-1];
					result.scores[j] = result.scores[j-1];
				}

				result.scores[n] = value;
				result.class_id[n] = i;
				break;
			}
		}
	} 

	return result;
}

bool CaffeWrapper::ExtractFeatures(const char *image_filepath, IMAGE_ORIENTATION orientation, CNNEngine::CNNFeature& feature)
{
	bool ok = false;

	const std::string layer_name("fc7");

	if(model->has_blob(layer_name))
	{
		PreprocessAndLoadImage(image_filepath, orientation);

		// classify the image
		float loss = 0.f;
		const vector<Blob<float>*>& output = model->ForwardPrefilled(&loss);

		const boost::shared_ptr<Blob<float> > feature_blob = model->blob_by_name(layer_name);

		const float *data = feature_blob->cpu_data();
		feature.count = 0;

		for(int i=0; i < feature_blob->count() && i < feature.MAX_COUNT; i++)
		{
			feature.values[i] = data[i];
			feature.count++;
		}

		ok = true;
	}
	
	return ok;
}