#include "sdf_3d_reconstruction/sdf_reconstruction.h"
#include <stdlib.h>

void SDF_Reconstruction::kinect_callback(
		const sensor_msgs::PointCloud2ConstPtr& ros_cloud) {
	cout << "callback!" << endl;
	frame_num++;

	CALLGRIND_START_INSTRUMENTATION
	;

	pcl::PCLPointCloud2 pcl_pc2;
	pcl_conversions::toPCL(*ros_cloud, pcl_pc2);
	pcl::PointCloud<pcl::PointXYZRGB>::Ptr pcl_cloud(
			new pcl::PointCloud<pcl::PointXYZRGB>);
	pcl::fromPCLPointCloud2(pcl_pc2, *pcl_cloud);
	pcl::PointCloud<pcl::PointXYZRGB>::Ptr cloud_filtered(
			new pcl::PointCloud<pcl::PointXYZRGB>);
	//fast bilateral filter for smoothing depth information in organized point clouds
	pcl::FastBilateralFilter<pcl::PointXYZRGB> bFilter;
	bFilter.setInputCloud(pcl_cloud);
//	bFilter.setHalfSize(5.0f);
//	bFilter.setStdDev(0.2f);
	bFilter.applyFilter(*cloud_filtered);
	// estimate normals
	pcl::PointCloud<pcl::Normal>::Ptr normals(new pcl::PointCloud<pcl::Normal>);
	pcl::IntegralImageNormalEstimation<pcl::PointXYZRGB, pcl::Normal> ne;
	ne.setNormalEstimationMethod(ne.AVERAGE_3D_GRADIENT);
	ne.setMaxDepthChangeFactor(0.02f);
	ne.setNormalSmoothingSize(10.0f);
	ne.setInputCloud(cloud_filtered);
	ne.compute(*normals);

	if (_useGroundTruth) {
		try {
			listener.waitForTransform("/world", "/openni_rgb_optical_frame",
					ros::Time(), ros::Duration(12.0));
			listener.lookupTransform("/world", "/openni_rgb_optical_frame",
					ros::Time(), transform);
		} catch (tf::TransformException ex) {
			ROS_ERROR("%s", ex.what());
			return;
		}
		Vector3d trans;
		Eigen::Quaterniond rot;
		tf::vectorTFToEigen(transform.getOrigin(), trans);
		tf::quaternionTFToEigen(transform.getRotation(), rot);
		Matrix3d rotMat = rot.toRotationMatrix();
		this->camera_tracking->set_camera_transformation(rotMat, trans);
	} else {

		if (frame_num > 1) {
			this->camera_tracking->estimate_new_position(sdf, cloud_filtered);
		}
	}
	sdf->update(this->camera_tracking, cloud_filtered, normals);

	CALLGRIND_STOP_INSTRUMENTATION
	;
	CALLGRIND_DUMP_STATS
	;
}

SDF_Reconstruction::SDF_Reconstruction() {
	Vector3d sdf_origin(-3.5, -3.5, -1.5);
	//m , width, height, depth, treshold
	sdf = new SDF(200, 7.0, 7.0, 3.0, sdf_origin, 0.3, 0.025);
	
	_useGroundTruth = false;
        this->camera_tracking = new CameraTracking(20,0.001,1.0,0.01, sdf);
	pcl = nh.subscribe("/camera/rgb/points", 1, &SDF_Reconstruction::kinect_callback, this);
	this->camera_tracking->cam_info = nh.subscribe("/camera/rgb/camera_info", 1,
			&CameraTracking::camera_info_cb, this->camera_tracking);
	frame_num = 0;
	//pub = nh.advertise<sensor_msgs::PointCloud2> ("/our_output/", 1);
	//Ros::Publisher(topic n)


	std::thread visualization_thread(&SDF::visualize, sdf, 0.2);
	int oldNumPub = pcl.getNumPublishers();
	while (ros::ok()) {
		if ((oldNumPub == 1) && (pcl.getNumPublishers() == 0)) {
			cout << "lost connection" << endl;
			sdf->finish_visualization_thread = true;
			visualization_thread.join();
			exit(0);
		}
		oldNumPub = pcl.getNumPublishers();
		ros::spinOnce();
	}

}
