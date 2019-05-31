#include "visualOdometry.h"
#include "PoseEstimator.h"

cv::Mat euler2rot(cv::Mat& rotationMatrix, const cv::Mat & euler)
{

  double x = euler.at<double>(0);
  double y = euler.at<double>(1);
  double z = euler.at<double>(2);

  // Assuming the angles are in radians.
  double ch = cos(z);
  double sh = sin(z);
  double ca = cos(y);
  double sa = sin(y);
  double cb = cos(x);
  double sb = sin(x);

  double m00, m01, m02, m10, m11, m12, m20, m21, m22;

  m00 = ch * ca;
  m01 = sh*sb - ch*sa*cb;
  m02 = ch*sa*sb + sh*cb;
  m10 = sa;
  m11 = ca*cb;
  m12 = -ca*sb;
  m20 = -sh*ca;
  m21 = sh*sa*cb + ch*sb;
  m22 = -sh*sa*sb + ch*cb;

  rotationMatrix.at<double>(0,0) = m00;
  rotationMatrix.at<double>(0,1) = m01;
  rotationMatrix.at<double>(0,2) = m02;
  rotationMatrix.at<double>(1,0) = m10;
  rotationMatrix.at<double>(1,1) = m11;
  rotationMatrix.at<double>(1,2) = m12;
  rotationMatrix.at<double>(2,0) = m20;
  rotationMatrix.at<double>(2,1) = m21;
  rotationMatrix.at<double>(2,2) = m22;

  return rotationMatrix;
}

void checkValidMatch(std::vector<cv::Point2f>& points, std::vector<cv::Point2f>& points_return, std::vector<bool>& status, int threshold)
{
    int offset;
    for (int i = 0; i < points.size(); i++)
    {
        offset = std::max(std::abs(points[i].x - points_return[i].x), std::abs(points[i].y - points_return[i].y));
        // std::cout << offset << ", ";

        if(offset > threshold)
        {
            status.push_back(false);
        }
        else
        {
            status.push_back(true);
        }
    }
}

void removeInvalidPoints(std::vector<cv::Point2f>& points, const std::vector<bool>& status)
{
    int index = 0;
    for (int i = 0; i < status.size(); i++)
    {
        if (status[i] == false)
        {
            points.erase(points.begin() + index);
        }
        else
        {
            index ++;
        }
    }
}

template<class T>
void removeInvalidElement(std::vector<T>& items, const std::vector<bool>& status)
{
	int index = 0;
	for (int i = 0; i < status.size(); i++)
	{
		if (status[i] == false)
		{
			items.erase(items.begin() + index);
		}
		else
		{
			index++;
		}
	}
}



void matchingFeatures(cv::Mat& imageLeft_t0, cv::Mat& imageRight_t0,
                      cv::Mat& imageLeft_t1, cv::Mat& imageRight_t1, 
                      FeatureSet& currentVOFeatures,
                      std::vector<cv::Point2f>&  pointsLeft_t0, 
                      std::vector<cv::Point2f>&  pointsRight_t0, 
                      std::vector<cv::Point2f>&  pointsLeft_t1, 
                      std::vector<cv::Point2f>&  pointsRight_t1)
{
    // ----------------------------
    // Feature detection using FAST
    // ----------------------------
    std::vector<cv::Point2f>  pointsLeftReturn_t0;   // feature points to check cicular mathcing validation


    if (currentVOFeatures.size() < 2000)
    {

        // append new features with old features
        appendNewFeatures(imageLeft_t0, currentVOFeatures);   
        // std::cout << "Current feature set size: " << currentVOFeatures.points.size() << std::endl;
    }

    // --------------------------------------------------------
    // Feature tracking using KLT tracker, bucketing and circular matching
    // --------------------------------------------------------
    int bucket_size = 50;
    int features_per_bucket = 4;
    bucketingFeatures(imageLeft_t0, currentVOFeatures, bucket_size, features_per_bucket);

    pointsLeft_t0 = currentVOFeatures.points;
    
    circularMatching(imageLeft_t0, imageRight_t0, imageLeft_t1, imageRight_t1,
                     pointsLeft_t0, pointsRight_t0, pointsLeft_t1, pointsRight_t1, pointsLeftReturn_t0, currentVOFeatures);

    std::vector<bool> status;
    checkValidMatch(pointsLeft_t0, pointsLeftReturn_t0, status, 0);

    removeInvalidPoints(pointsLeft_t0, status);
    removeInvalidPoints(pointsLeft_t1, status);
    removeInvalidPoints(pointsRight_t0, status);
    removeInvalidPoints(pointsRight_t1, status);

    currentVOFeatures.points = pointsLeft_t1;

}


void trackingFrame2Frame(cv::Mat& projMatrl, cv::Mat& projMatrr,
                         std::vector<cv::Point2f>&  pointsLeft_t0,
                         std::vector<cv::Point2f>&  pointsLeft_t1, 
                         cv::Mat& points3D_t0,
                         cv::Mat& rotation,
                         cv::Mat& translation)
{

      // Calculate frame to frame transformation

      // -----------------------------------------------------------
      // Rotation(R) estimation using Nister's Five Points Algorithm
      // -----------------------------------------------------------
      double focal = projMatrl.at<float>(0, 0);
      cv::Point2d principle_point(projMatrl.at<float>(0, 2), projMatrl.at<float>(1, 2));

      //recovering the pose and the essential cv::matrix
      cv::Mat E, mask;
      cv::Mat translation_mono = cv::Mat::zeros(3, 1, CV_64F);
      E = cv::findEssentialMat(pointsLeft_t1, pointsLeft_t0, focal, principle_point, cv::RANSAC, 0.999, 1.0, mask);
      cv::recoverPose(E, pointsLeft_t1, pointsLeft_t0, rotation, translation_mono, focal, principle_point, mask);
      // std::cout << "recoverPose rotation: " << rotation << std::endl;

      // ------------------------------------------------
      // Translation (t) estimation by use solvePnPRansac
      // ------------------------------------------------
      cv::Mat distCoeffs = cv::Mat::zeros(4, 1, CV_64FC1);  
      cv::Mat inliers;  
      cv::Mat rvec = cv::Mat::zeros(3, 1, CV_64FC1);
      cv::Mat intrinsic_matrix = (cv::Mat_<float>(3, 3) << projMatrl.at<float>(0, 0), projMatrl.at<float>(0, 1), projMatrl.at<float>(0, 2),
                                                   projMatrl.at<float>(1, 0), projMatrl.at<float>(1, 1), projMatrl.at<float>(1, 2),
                                                   projMatrl.at<float>(1, 1), projMatrl.at<float>(1, 2), projMatrl.at<float>(1, 3));

      int iterationsCount = 200;        // number of Ransac iterations.
      float reprojectionError = 2.0;    // maximum allowed distance to consider it an inlier.
      float confidence = 0.95;          // RANSAC successful confidence.
      bool useExtrinsicGuess = true;
      int flags =cv::SOLVEPNP_EPNP;

      cv::solvePnPRansac( points3D_t0, pointsLeft_t1, intrinsic_matrix, distCoeffs, rvec, translation,
                          useExtrinsicGuess, iterationsCount, reprojectionError, confidence,
                          inliers, flags );

      cv::Rodrigues(rvec, rotation);
      rotation = rotation.t();
      translation = -translation;
      // std::cout << "inliers size: " << inliers.size() << std::endl;

}

void displayTracking(cv::Mat& imageLeft_t1, 
                     std::vector<cv::Point2f>&  pointsLeft_t0,
                     std::vector<cv::Point2f>&  pointsLeft_t1)
{
	TicTok tic;
      // -----------------------------------------
      // Display feature racking
      // -----------------------------------------
      int radius = 2;
      cv::Mat vis;

      cv::cvtColor(imageLeft_t1, vis, CV_GRAY2BGR, 3);


      for (int i = 0; i < pointsLeft_t0.size(); i++)
      {
          cv::circle(vis, cvPoint(pointsLeft_t0[i].x, pointsLeft_t0[i].y), radius, CV_RGB(0,255,0));
      }

      for (int i = 0; i < pointsLeft_t1.size(); i++)
      {
          cv::circle(vis, cvPoint(pointsLeft_t1[i].x, pointsLeft_t1[i].y), radius, CV_RGB(255,0,0));
      }

      for (int i = 0; i < pointsLeft_t1.size(); i++)
      {
          cv::line(vis, pointsLeft_t0[i], pointsLeft_t1[i], CV_RGB(0,255,0));
      }

      cv::imshow("vis ", vis );  
	  std::cout << "display tracking: " << tic.tok() << "ms" << std::endl;
}

void displayTracking(cv::Mat& imageLeft_t1,
	std::vector<cv::Point2f>&  pointsLeft_t0,
	std::vector<cv::Point2f>&  pointsLeft_t1,
	cv::Point2f epipoint)
{
	TicTok tic;
	// -----------------------------------------
	// Display feature racking
	// -----------------------------------------
	int radius = 2;
	cv::Mat vis;

	cv::cvtColor(imageLeft_t1, vis, CV_GRAY2BGR, 3);


	for (int i = 0; i < pointsLeft_t0.size(); i++)
	{
		cv::circle(vis, cvPoint(pointsLeft_t0[i].x, pointsLeft_t0[i].y), radius, CV_RGB(0, 255, 0));
	}

	for (int i = 0; i < pointsLeft_t1.size(); i++)
	{
		cv::circle(vis, cvPoint(pointsLeft_t1[i].x, pointsLeft_t1[i].y), radius, CV_RGB(255, 0, 0));
	}

	for (int i = 0; i < pointsLeft_t1.size(); i++)
	{
		cv::line(vis, pointsLeft_t0[i], pointsLeft_t1[i], CV_RGB(0, 255, 0));
	}

	cv::circle(vis, epipoint, 10, CV_RGB(255, 255, 0), 4);

	cv::imshow("vis ", vis);
	std::cout << "display tracking: " << tic.tok() << "ms" << std::endl;
}

void visualOdometry(int current_frame_id, std::string filepath,
                    cv::Mat& projMatrl, cv::Mat& projMatrr,
                    cv::Mat& rotation, cv::Mat& translation_mono, cv::Mat& translation_stereo, 
                    cv::Mat& image_left_t0,
                    cv::Mat& image_right_t0,
                    FeatureSet& current_features,
                    cv::Mat& points4D_t0)
{

    // ------------
    // Load images
    // ------------
    cv::Mat image_left_t1_color,  image_left_t1;
    loadImageLeft(image_left_t1_color,  image_left_t1, current_frame_id + 1, filepath);
    
    cv::Mat image_right_t1_color, image_right_t1;  
    loadImageRight(image_right_t1_color, image_right_t1, current_frame_id + 1, filepath);

    // ----------------------------
    // Feature detection using FAST
    // ----------------------------
    std::vector<cv::Point2f>  points_left_t0, points_right_t0, points_left_t1, points_right_t1, points_left_t0_return;   //vectors to store the coordinates of the feature points

    if (current_features.size() < 2000)
    {
        // use all new features
        // featureDetectionFast(image_left_t0, current_features.points);     
        // current_features.ages = std::vector<int>(current_features.points.size(), 0);

        // append new features with old features
        appendNewFeatures(image_left_t0, current_features);   

        std::cout << "Current feature set size: " << current_features.points.size() << std::endl;
    }


    // --------------------------------------------------------
    // Feature tracking using KLT tracker, bucketing and circular matching
    // --------------------------------------------------------
    int bucket_size = 50;
    int features_per_bucket = 4;
    bucketingFeatures(image_left_t0, current_features, bucket_size, features_per_bucket);

    points_left_t0 = current_features.points;
    
    circularMatching(image_left_t0, image_right_t0, image_left_t1, image_right_t1,
                     points_left_t0, points_right_t0, points_left_t1, points_right_t1, points_left_t0_return, current_features);

    std::vector<bool> status;
    checkValidMatch(points_left_t0, points_left_t0_return, status, 0);

    removeInvalidPoints(points_left_t0, status);
    removeInvalidPoints(points_left_t0_return, status);
    removeInvalidPoints(points_left_t1, status);
    removeInvalidPoints(points_right_t0, status);

    current_features.points = points_left_t1;

    // -----------------------------------------------------------
    // Rotation(R) estimation using Nister's Five Points Algorithm
    // -----------------------------------------------------------
    double focal = projMatrl.at<float>(0, 0);
    cv::Point2d principle_point(projMatrl.at<float>(0, 2), projMatrl.at<float>(1, 2));

    //recovering the pose and the essential cv::matrix
    cv::Mat E, mask;
    E = cv::findEssentialMat(points_left_t1, points_left_t0, focal, principle_point, cv::RANSAC, 0.999, 1.0, mask);
    cv::recoverPose(E, points_left_t1, points_left_t0, rotation, translation_mono, focal, principle_point, mask);

    // ---------------------
    // Triangulate 3D Points
    // ---------------------
    cv::Mat points3D_t0;
    cv::triangulatePoints( projMatrl,  projMatrr,  points_left_t0,  points_right_t0,  points4D_t0);

    cv::convertPointsFromHomogeneous(points4D_t0.t(), points3D_t0);


    // ------------------------------------------------
    // Translation (t) estimation by use solvePnPRansac
    // ------------------------------------------------
    cv::Mat distCoeffs = cv::Mat::zeros(4, 1, CV_64FC1);  
    cv::Mat inliers;  
    cv::Mat rvec = cv::Mat::zeros(3, 1, CV_64FC1);
    cv::Mat intrinsic_matrix = (cv::Mat_<float>(3, 3) << projMatrl.at<float>(0, 0), projMatrl.at<float>(0, 1), projMatrl.at<float>(0, 2),
                                                 projMatrl.at<float>(1, 0), projMatrl.at<float>(1, 1), projMatrl.at<float>(1, 2),
                                                 projMatrl.at<float>(1, 1), projMatrl.at<float>(1, 2), projMatrl.at<float>(1, 3));

    int iterationsCount = 500;        // number of Ransac iterations.
    float reprojectionError = 2.0;    // maximum allowed distance to consider it an inlier.
    float confidence = 0.95;          // RANSAC successful confidence.
    bool useExtrinsicGuess = true;
    int flags =cv::SOLVEPNP_ITERATIVE;

    cv::solvePnPRansac( points3D_t0, points_left_t1, intrinsic_matrix, distCoeffs, rvec, translation_stereo,
                        useExtrinsicGuess, iterationsCount, reprojectionError, confidence,
                        inliers, flags );

    std::cout << "inliers size: " << inliers.size() << std::endl;

    // translation_stereo = -translation_stereo;

    // std::cout << "rvec : " <<rvec <<std::endl;
    // std::cout << "translation_stereo : " <<translation_stereo <<std::endl;

    // -----------------------------------------
    // Prepare image for next frame
    // -----------------------------------------
    image_left_t0 = image_left_t1;
    image_right_t0 = image_right_t1;


    // -----------------------------------------
    // Display
    // -----------------------------------------

    int radius = 2;
    // cv::Mat vis = image_left_t0.clone();

    cv::Mat vis;

    cv::cvtColor(image_left_t1, vis, CV_GRAY2BGR, 3);


    for (int i = 0; i < points_left_t0.size(); i++)
    {
        circle(vis, cvPoint(points_left_t0[i].x, points_left_t0[i].y), radius, CV_RGB(0,255,0));
    }

    for (int i = 0; i < points_left_t1.size(); i++)
    {
        circle(vis, cvPoint(points_left_t1[i].x, points_left_t1[i].y), radius, CV_RGB(255,0,0));
    }

    for (int i = 0; i < points_left_t1.size(); i++)
    {
        cv::line(vis, points_left_t0[i], points_left_t1[i], CV_RGB(0,255,0));
    }

    imshow("vis ", vis );
    
}

namespace MVSO {

MultiViewStereoOdometry::MultiViewStereoOdometry(const std::string &settingPath)
{
    // 相机参数
    std::cout << "Calibration Filepath: " << settingPath << std::endl;

    cv::FileStorage fSettings(settingPath, cv::FileStorage::READ);

    float fx = fSettings["Camera.fx"];
    float fy = fSettings["Camera.fy"];
    float cx = fSettings["Camera.cx"];
    float cy = fSettings["Camera.cy"];
    float bf = fSettings["Camera.bf"];
    camera_ = CameraModel(fx, fy, cx, cy, bf);
}

cv::Mat MultiViewStereoOdometry::grabImage(cv::Mat imgLeft, cv::Mat imgRight)
{
    currentFrame_ = std::make_shared<Frame>(imgLeft, imgRight);
	if (currentFrame_->frameId_ == 0)
	{
		lastFrame_ = currentFrame_;
		return cv::Mat();
	}
    tracking();
	return pose_;
}

cv::Mat MultiViewStereoOdometry::tracking()
{
    std::vector<cv::Point2f> ptsleft_0, ptsright_0, ptsright_1, ptsleft_1;
    matchingFeatures(lastFrame_->getLeftImg(), lastFrame_->getRightImg(),
                     currentFrame_->getLeftImg(), currentFrame_->getRightImg(),
                     ptsleft_0, ptsright_0, ptsleft_1, ptsright_1);

	std::vector<cv::Point2f>& currentPointsLeft_t0 = ptsleft_0;
	std::vector<cv::Point2f>& currentPointsLeft_t1 = ptsleft_1;

	// std::cout << "oldPointsLeft_t0 size : " << oldPointsLeft_t0.size() << std::endl;
	// std::cout << "currentFramePointsLeft size : " << currentPointsLeft_t0.size() << std::endl;

	std::vector<cv::Point2f> newPoints;
	std::vector<bool> valid; // valid new points are ture

	// ---------------------
	// Triangulate 3D Points
	// ---------------------
	cv::Mat points3D_t0, points4D_t0;
	cv::triangulatePoints(
		camera_.getLeftProjectionMatrix(),
		camera_.getRightProjectionMatrix(),
		ptsleft_0, ptsright_0, points4D_t0);

	cv::convertPointsFromHomogeneous(points4D_t0.t(), points3D_t0);


	std::vector<cv::Point3f> points3d_t0(points3D_t0);


	// std::cout << "points4D_t0 size : " << points4D_t0.size() << std::endl;

	cv::Mat points3D_t1, points4D_t1;
	// std::cout << "pointsLeft_t1 size : " << pointsLeft_t1.size() << std::endl;
	// std::cout << "pointsRight_t1 size : " << pointsRight_t1.size() << std::endl;

	cv::triangulatePoints(
		camera_.getLeftProjectionMatrix(),
		camera_.getRightProjectionMatrix(),
		ptsleft_1, ptsright_1, points4D_t1);
	cv::convertPointsFromHomogeneous(points4D_t1.t(), points3D_t1);


	std::vector<cv::Point3f> points3d_t1(points3D_t1);


	// std::cout << "points4D_t1 size : " << points4D_t1.size() << std::endl;

	// ---------------------
	// Tracking transfomation
	// ---------------------
	PoseEstimator estimator(camera_);
	pose_ = estimator.estimatePose(ptsleft_0, ptsleft_1, points3d_t0);

	//pose_ = estimator.estimatePose(ptsleft_1, ptsleft_0, points3d_t1);
	//{
	//	cv::Mat r = pose_.colRange(0, 3);
	//	cv::Mat t = pose_.col(3);
	//	r = r.t();
	//	t = -r * t;
	//}


	//pose_ = estimator.estimatePose(ptsleft_0, ptsleft_1, points3d_t0, points3d_t1);



	//trackingFrame2Frame(ptsleft_0, ptsleft_1, points3D_t0);


	cv::Mat r = pose_.colRange(0, 3);
	cv::Mat t = pose_.col(3);
	cv::Point3f camera_center;
	camera_center.x = t.at<double>(0);
	camera_center.y = t.at<double>(1);
	camera_center.z = t.at<double>(2);

	cv::Point2f epipoint;
	epipoint.x = camera_.fx_*camera_center.x / camera_center.z + camera_.cx_;
	epipoint.y = camera_.fy_*camera_center.y / camera_center.z + camera_.cy_;

	displayTracking(currentFrame_->getLeftImg(), ptsleft_0, ptsleft_1, epipoint);

	return pose_.clone();
}

void MultiViewStereoOdometry::matchingFeatures(
	cv::Mat &imageLeft_t0,
	cv::Mat &imageRight_t0,
	cv::Mat &imageLeft_t1,
	cv::Mat &imageRight_t1,
	std::vector<cv::Point2f> &pointsLeft_t0,
	std::vector<cv::Point2f> &pointsRight_t0,
	std::vector<cv::Point2f> &pointsLeft_t1,
	std::vector<cv::Point2f> &pointsRight_t1)
{
    // ----------------------------
    // Feature detection using FAST
    // ----------------------------
    std::vector<cv::Point2f>  pointsLeftReturn_t0;   // feature points to check cicular mathcing validation

    int features_per_bucket = 1;

    lastFrame_->prepareFeature();
	lastFrame_->bucketingFeature(features_per_bucket);
    // --------------------------------------------------------
    // Feature tracking using KLT tracker, bucketing and circular matching
    // --------------------------------------------------------

    pointsLeft_t0 = lastFrame_->getKeypoints();

    circularMatching(pointsLeft_t0, pointsRight_t0, pointsLeft_t1, pointsRight_t1, pointsLeftReturn_t0);

    std::vector<bool> status;
    checkValidMatch(pointsLeft_t0, pointsLeftReturn_t0, status, 0);

    removeInvalidPoints(pointsLeft_t0, status);
    removeInvalidPoints(pointsLeft_t1, status);
    removeInvalidPoints(pointsRight_t0, status);
    removeInvalidPoints(pointsRight_t1, status);

	removeInvalidElement(lastFrame_->pointAges_, status);

	lastFrame_ = currentFrame_;

}

void MultiViewStereoOdometry::circularMatching(
	std::vector<cv::Point2f>& pointsLeft_t0,
	std::vector<cv::Point2f>& pointsRight_t0,
	std::vector<cv::Point2f>& pointsLeft_t1,
	std::vector<cv::Point2f>& pointsRight_t1,
	std::vector<cv::Point2f>& pointsLeft_t0_return)
{

	//this function automatically gets rid of points for which tracking fails

	std::vector<float> err;
	cv::Size winSize = cv::Size(21, 21);
	//cv::Size winSizeStereo = cv::Size(31, 15);
	cv::Size winSizeStereo = cv::Size(21, 21);

	cv::TermCriteria termcrit = cv::TermCriteria(cv::TermCriteria::COUNT + cv::TermCriteria::EPS, 30, 0.01);

	std::vector<uchar> status0;
	std::vector<uchar> status1;
	std::vector<uchar> status2;
	std::vector<uchar> status3;

	TicTok tic;
	calcOpticalFlowPyrLK(lastFrame_->getLeftImg(), lastFrame_->getRightImg(), pointsLeft_t0, pointsRight_t0, status0, err, winSize, 3, termcrit, 0, 0.001);
	calcOpticalFlowPyrLK(lastFrame_->getRightImg(), currentFrame_->getRightImg(), pointsRight_t0, pointsRight_t1, status1, err, winSizeStereo, 3, termcrit, 0, 0.001);
	calcOpticalFlowPyrLK(currentFrame_->getRightImg(), currentFrame_->getLeftImg(), pointsRight_t1, pointsLeft_t1, status2, err, winSize, 3, termcrit, 0, 0.001);
	calcOpticalFlowPyrLK(currentFrame_->getLeftImg(), lastFrame_->getLeftImg(), pointsLeft_t1, pointsLeft_t0_return, status3, err, winSizeStereo, 3, termcrit, 0, 0.001);

	std::cerr << "calcOpticalFlowPyrLK time: " << tic.tok() << "ms" << std::endl;


	deleteUnmatchFeaturesCircle(pointsLeft_t0, pointsRight_t0, pointsRight_t1, pointsLeft_t1, pointsLeft_t0_return,
		status0, status1, status2, status3);


}

void MultiViewStereoOdometry::deleteUnmatchFeaturesCircle(std::vector<cv::Point2f>& points0, std::vector<cv::Point2f>& points1, std::vector<cv::Point2f>& points2, std::vector<cv::Point2f>& points3, std::vector<cv::Point2f>& points0_return, std::vector<uchar>& status0, std::vector<uchar>& status1, std::vector<uchar>& status2, std::vector<uchar>& status3)
{

	//getting rid of points for which the KLT tracking failed or those who have gone outside the frame
	for (int i = 0; i < lastFrame_->pointAges_.size(); ++i)
	{
		++lastFrame_->pointAges_[i];
	}

	int indexCorrection = 0;
	for (int i = 0; i < status3.size(); i++)
	{
		cv::Point2f pt0 = points0.at(i - indexCorrection);
		cv::Point2f pt1 = points1.at(i - indexCorrection);
		cv::Point2f pt2 = points2.at(i - indexCorrection);
		cv::Point2f pt3 = points3.at(i - indexCorrection);
		cv::Point2f pt0_r = points0_return.at(i - indexCorrection);

		if ((status3.at(i) == 0) || (pt3.x < 0) || (pt3.y < 0) ||
			(status2.at(i) == 0) || (pt2.x < 0) || (pt2.y < 0) ||
			(status1.at(i) == 0) || (pt1.x < 0) || (pt1.y < 0) ||
			(status0.at(i) == 0) || (pt0.x < 0) || (pt0.y < 0))
		{
			if ((pt0.x < 0) || (pt0.y < 0) || (pt1.x < 0) || (pt1.y < 0) || (pt2.x < 0) || (pt2.y < 0) || (pt3.x < 0) || (pt3.y < 0))
			{
				status3.at(i) = 0;
			}
			points0.erase(points0.begin() + (i - indexCorrection));
			points1.erase(points1.begin() + (i - indexCorrection));
			points2.erase(points2.begin() + (i - indexCorrection));
			points3.erase(points3.begin() + (i - indexCorrection));
			points0_return.erase(points0_return.begin() + (i - indexCorrection));

			lastFrame_->pointAges_.erase(lastFrame_->pointAges_.begin() + (i - indexCorrection));
			indexCorrection++;
		}

	}

}

void MultiViewStereoOdometry::trackingFrame2Frame(std::vector<cv::Point2f>& pointsLeft_t0, std::vector<cv::Point2f>& pointsLeft_t1, cv::Mat & points3D_t0)
{
	// Calculate frame to frame transformation

	cv::Mat rotation, translation;

  // -----------------------------------------------------------
  // Rotation(R) estimation using Nister's Five Points Algorithm
  // -----------------------------------------------------------
	double focal = camera_.fx_;
	cv::Point2d principle_point(camera_.cx_, camera_.cy_);

	//recovering the pose and the essential cv::matrix
	cv::Mat E, mask;
	cv::Mat translation_mono = cv::Mat::zeros(3, 1, CV_64F);
	E = cv::findEssentialMat(pointsLeft_t1, pointsLeft_t0, focal, principle_point, cv::RANSAC, 0.999, 1.0, mask);
	cv::recoverPose(E, pointsLeft_t1, pointsLeft_t0, rotation, translation_mono, focal, principle_point, mask);
	// std::cout << "recoverPose rotation: " << rotation << std::endl;

	// ------------------------------------------------
	// Translation (t) estimation by use solvePnPRansac
	// ------------------------------------------------
	cv::Mat distCoeffs = cv::Mat::zeros(4, 1, CV_64FC1);
	cv::Mat inliers;
	cv::Mat rvec = cv::Mat::zeros(3, 1, CV_64FC1);

	int iterationsCount = 200;        // number of Ransac iterations.
	float reprojectionError = 2.0;    // maximum allowed distance to consider it an inlier.
	float confidence = 0.95;          // RANSAC successful confidence.
	bool useExtrinsicGuess = true;
	int flags = cv::SOLVEPNP_EPNP;

	cv::solvePnPRansac(points3D_t0, pointsLeft_t1, camera_.intrinsicMat_, distCoeffs, rvec, translation,
		useExtrinsicGuess, iterationsCount, reprojectionError, confidence,
		inliers, flags);

	cv::Rodrigues(rvec, rotation);
	rotation = rotation.t();
	translation = -translation;
	// std::cout << "inliers size: " << inliers.size() << std::endl;

	cv::hconcat(rotation, translation, pose_);
}


}


