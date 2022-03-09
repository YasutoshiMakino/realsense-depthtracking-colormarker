// Realsense_depth_check.cpp : このファイルには 'main' 関数が含まれています。プログラム実行の開始と終了がそこで行われます。
//

#include "stdafx.h"
#include <opencv2/opencv.hpp>
#include <iostream>
#include <fstream>

#include <librealsense2/rs.hpp> // Include RealSense Cross Platform API
#include <librealsense2/rsutil.h>

#include <stdio.h>
#include <windows.h>
#include <string.h>

#include <chrono>
#include <ctime> 

#define ZERO_DEP 0.35		//基準深さ(m)．これより深い方に正の値となるように計算
#define DRAW_SCALE 50000	//グラフ描画用のスケール（メートル×DRAW_SCALE）10000で，1mmの変化を 10 pixelに
#define PICTURE_SCALE 0.75  //画像描画用の比率
#define AVE_DATALENGTH 5	//平均を取るデータの長さ
#define FRAMERATE 30		//30fps
#define DRAW_WID_TIME 5		//何秒分グラフを描画するか
#define ROI_SIZE 50			//ROIの一辺の長さ（偶数で指定）
#define HSV_RANGE 20		//HSVでカラー指定するときの許容幅
#define DATA_SIZE 100000	//記録用のデータ配列の数


using std::endl;
using std::ofstream;

bool flag = true;
bool target_color_flg = false;
bool offset_depth = false;
bool image_flg = false;
bool window_init = false;

int width_depth, height_depth, width_color, height_color;
double zero_depth = 0.0;

cv::Mat image_color;
cv::Mat image_depth;
cv::Mat image_roi, image_roi_hsv;
cv::Rect roi_rect, super_rect;
cv::Mat mask, mask2, image_color_hsv;

rs2::pipeline pipe;

int Picked_H = 0;
int Picked_S = 0;
int Picked_V = 0;

cv::Point3d PointData[DATA_SIZE];
std::chrono::duration<double> Now_time[DATA_SIZE];

void Switch() {
	while (true) {
		std::string input;
		std::cin >> input;
		std::cout << "###SYSTEM CLOSING...###" << std::endl;
		if (input == "q") {
			std::cout << "###SYSTEM CLOSING...###" << std::endl;
			flag = false;
			break;
		}
	}
}

//平均
double average(double data[]) {
	double sum = 0.0;
	double ave = 0.0;
	for (int i = 0; i < AVE_DATALENGTH; i++) sum += data[i];

	ave = sum / AVE_DATALENGTH;

	return ave;
}

//描画
void draw_graph(cv::Mat img, double data[]) {
	int delta_t = (int)(img.cols / (FRAMERATE * DRAW_WID_TIME))+1;	//丸めの分，＋1をしないとグラフが画像の端まで行かずに切れる
	for (int i = 0; i < FRAMERATE * DRAW_WID_TIME; i++) {
		cv::line(img, cv::Point(delta_t * i, (img.rows - (int)(data[i] * DRAW_SCALE))), cv::Point(delta_t * (i + 1), (img.rows - (int)(data[i+1] * DRAW_SCALE))), cv::Scalar(255, 255, 255), 3 );
	}
	//printf("%f, %d \n", data[0], (int)(data[0] * DRAW_SCALE));
}

std::string float_to_string(float f, int digits)

{

	std::ostringstream oss;
	oss << std::setprecision(digits) << std::setiosflags(std::ios::fixed) << f;
	return oss.str();

}



cv::Point2i MousePos(0, 0);	//マウスの位置
cv::Point2i PreROIPos(0, 0); //ROI選択時の位置
cv::Point2i ROIPos(0, 0);	//ROIの位置

void mouseCallback(int event, int x, int y, int flags, void* userdata)
{
	// マウスの座標を出力
//	std::cout << "x=" << x << ", y=" << y << " " "\n";
	MousePos.x = x /PICTURE_SCALE;
	MousePos.y = y/PICTURE_SCALE;

	PreROIPos.x = MousePos.x - ROI_SIZE / 2;
	PreROIPos.y = MousePos.y - ROI_SIZE / 2;

	if (MousePos.x < 0) PreROIPos.x = 0;
	else if (MousePos.x > width_color) PreROIPos.x = width_color - ROI_SIZE;

	if (MousePos.y < 0) PreROIPos.y = 0;
	else if (MousePos.y > height_color) PreROIPos.y = height_color - ROI_SIZE;


	// イベントの種類を出力
	switch (event) {
	case cv::EVENT_MOUSEMOVE:
		//		std::cout << "マウスが動いた";
		break;
	case cv::EVENT_LBUTTONDOWN:
		//		std::cout << "左ボタンを押した";
		break;
	case cv::EVENT_RBUTTONDOWN:
		//		std::cout << "右ボタンを押した";
		break;
	case cv::EVENT_LBUTTONUP:
		//		std::cout << "左ボタンを離した";
		offset_depth = false;
		ROIPos.x = PreROIPos.x;
		ROIPos.y = PreROIPos.y;

		//クリックした場所のHSV情報を取得
		Picked_H = image_color_hsv.at<cv::Vec3b>(ROIPos.y + ROI_SIZE / 2, ROIPos.x + ROI_SIZE / 2)[0];
		Picked_S = image_color_hsv.at<cv::Vec3b>(ROIPos.y + ROI_SIZE / 2, ROIPos.x + ROI_SIZE / 2)[1];
		Picked_V = image_color_hsv.at<cv::Vec3b>(ROIPos.y + ROI_SIZE / 2, ROIPos.x + ROI_SIZE / 2)[2];
		std::cout << "H=" << Picked_H * 2 << ", S=" << Picked_S *100/255 << ", V=" << Picked_V *100/255 << "\n";

		break;
	case cv::EVENT_RBUTTONUP:
		//		std::cout << "右ボタンを離した";
		//オフセットだけ更新
		offset_depth = false;
		break;
	case cv::EVENT_RBUTTONDBLCLK:
		//		std::cout << "右ボタンをダブルクリック";
		break;
	case cv::EVENT_LBUTTONDBLCLK:
		//		std::cout << "左ボタンをダブルクリック";
		image_flg = !image_flg;
		break;
	}
}

//画像重ね描き用
//https://note.com/npaka/n/nddb33be1b782#KVCie
void overlayImage(cv::Mat* src, cv::Mat* overlay, const cv::Point& location) {
	for (int y = max(location.y, 0); y < src->rows; ++y) {
		int fY = y - location.y;
		if (fY >= overlay->rows)
			break;
		for (int x = max(location.x, 0); x < src->cols; ++x) {
			int fX = x - location.x;
			if (fX >= overlay->cols)
				break;
			double opacity = ((double)overlay->data[fY * overlay->step + fX * overlay->channels() + 3]) / 255;
			for (int c = 0; opacity > 0 && c < src->channels(); ++c) {
				unsigned char overlayPx = overlay->data[fY * overlay->step + fX * overlay->channels() + c];
				unsigned char srcPx = src->data[y * src->step + x * src->channels() + c];
//				src->data[y * src->step + src->channels() * x + c] = srcPx * (1. - opacity) + overlayPx * opacity;
				src->data[y * src->step + src->channels() * x + c] = overlayPx;
			}
		}
	}
}

int main(int argc, char* argv[]) try
{
	//データ保存用
	for (int i = 0; i < DATA_SIZE; i++) {
		PointData[i].x = 0;
		PointData[i].y = 0;
		PointData[i].z = 0;
	}
	int idx = 0;

	//RealSense#################
	std::thread th_a(Switch);

	// Declare depth colorizer for pretty visualization of depth data
	rs2::colorizer color_map;

	//色の設定を行う。標準はOpenGL用にRGBになっている。OPenCVはBGRで扱うため、ここを設定しないと色がバグる。
	rs2::config cfg;
	cfg.enable_stream(RS2_STREAM_COLOR, 1280, 720, RS2_FORMAT_BGR8, 30);
	cfg.enable_stream(RS2_STREAM_DEPTH, 1280, 720, RS2_FORMAT_Z16, 30);

	// Declare RealSense pipeline, encapsulating the actual device and sensors

	rs2::pipeline_profile pipe_profile = pipe.start(cfg);
	//内部パラメータ取得
	rs2::stream_profile color_stream = pipe_profile.get_stream(RS2_STREAM_COLOR);
	const rs2_intrinsics intrinsics = color_stream.as<rs2::video_stream_profile>().get_intrinsics();

	const auto window_name_color = "Color Image";
	//cv::namedWindow(window_name_color, cv::WINDOW_AUTOSIZE);
	//const auto window_name_depth = "Depth Image";
	//cv::namedWindow(window_name_depth, cv::WINDOW_AUTOSIZE);

	//Depthとcolorをあわせる処理用
	rs2::align align_to_depth(RS2_STREAM_DEPTH);
	rs2::align align_to_color(RS2_STREAM_COLOR);

	float depth_target = 0.0;
	float depth_pre = 0.0;
	int count = 0;

	//最初に幅と高さを取得するために，ループの外で1回だけ計測
	rs2::frameset data = pipe.wait_for_frames(); // Wait for next set of frames from the camera
	data = align_to_color.process(data);//colorにdepthをそろえる
										//data = align_to_depth.process(data);
	auto depth_data = data.get_depth_frame();
	rs2::frame depth = depth_data.apply_filter(color_map);
	rs2::frame color = data.get_color_frame();

	// Query frame size (width and height)
	width_depth = depth.as<rs2::video_frame>().get_width();
	height_depth = depth.as<rs2::video_frame>().get_height();
	width_color = color.as<rs2::video_frame>().get_width();
	height_color = color.as<rs2::video_frame>().get_height();

	cv::Mat show_color, show_depth, show_mask;
	double past_data[FRAMERATE*DRAW_WID_TIME];			//過去の値格納用配列
	double past_data_ave[FRAMERATE * DRAW_WID_TIME];	//過去の値の平均データ格納用配列
	for (int i = 0; i < FRAMERATE * DRAW_WID_TIME; i++) {	//初期化
		past_data[i] = 0.0;	
		past_data_ave[i] = 0.0;
	}
	double ave;						//AVE_DATALENGTH分の平均

	//画像重ね描き
	//アルファチャンネルまで込で読み込み
	cv::Mat superimpose_img = cv::imread("c:\\picture.png", cv::IMREAD_UNCHANGED);

	auto start = std::chrono::system_clock::now();
	std::time_t start_time = std::chrono::system_clock::to_time_t(start);

	while (flag)
	{
		data = pipe.wait_for_frames(); // Wait for next set of frames from the camera
		data = align_to_color.process(data);//colorにdepthをそろえる
											//data = align_to_depth.process(data);
		auto depth_data = data.get_depth_frame();
		depth = depth_data.apply_filter(color_map);
		color = data.get_color_frame();

		// Create OpenCV matrix of size (w,h) from the colorized depth data
		//void*は汎用ポインタを指す。
		//Matを定義するときにdepth.get_data()のアドレスを入れる必要があるが型エラーがおきるため、まず汎用ポインタにキャストしてから代入している。
		//データがコピーされるわけでなく、Matの指す先頭アドレスが指定のvoid*に変化する。
		image_depth = cv::Mat(cv::Size(width_depth, height_depth), CV_8UC3, (void*)depth.get_data(), cv::Mat::AUTO_STEP);
		image_color = cv::Mat(cv::Size(width_color, height_color), CV_8UC3, (void*)color.get_data(), cv::Mat::AUTO_STEP);

	//	resize(image_color, image_color, cv::Size(), PICTURE_SCALE, PICTURE_SCALE);
		roi_rect = cv::Rect{ (int)ROIPos.x, (int)ROIPos.y, ROI_SIZE, ROI_SIZE };

		//重ね描き領域の設定
		int super_x = (int)ROIPos.x + ROI_SIZE / 2 - superimpose_img.size().width/2;
		int super_y = (int)ROIPos.y + ROI_SIZE / 2 - superimpose_img.size().height/2;
		if (super_x < 0) super_x = 0;
		if (super_y < 0) super_y = 0;
		super_rect = cv::Rect{ super_x, super_y, superimpose_img.size().width, superimpose_img.size().height};

		image_roi = image_color(roi_rect);

		//赤い四角を描画,クリックされたらROIに設定
		if(!image_flg) cv::rectangle(image_color, cv::Point(ROIPos.x, ROIPos.y), cv::Point(ROIPos.x + ROI_SIZE, ROIPos.y + ROI_SIZE), cv::Scalar(200, 200, 200), 2, 4);

		//HSVに変換
		cv::cvtColor(image_color, image_color_hsv, cv::COLOR_BGR2HSV);
		//		image_roi_hsv = image_color_hsv(roi_rect);

				//ROIを設定してトラッキング
		roi_rect = cv::Rect{ (int)ROIPos.x, (int)ROIPos.y, ROI_SIZE, ROI_SIZE };
		image_roi = image_color_hsv(roi_rect);

		//カラートラッキング
		//上で指定したターゲットカラーの±HSV_RANGEの中で探索
		int Low_H = Picked_H - HSV_RANGE;
		int High_H = Picked_H + HSV_RANGE;
		if (Low_H < 0) Low_H = 0;
		if (High_H > 180) High_H = 180;

		int Low_S = Picked_S - HSV_RANGE * 3;
		int High_S = Picked_S + HSV_RANGE * 3;
		if (Low_S < 0) Low_S = 0;
		if (High_S > 255) High_S = 255;

		int Low_V = Picked_V - HSV_RANGE * 3;
		int High_V = Picked_V + HSV_RANGE * 3;

		if (Low_V < 0) Low_V = 0;
		if (High_V > 255) High_V = 255;


		//		std::cout << "H=" << Picked_H << ", S=" << Picked_S << ", V=" << Picked_V << "\n";

		cv::inRange(image_roi, cv::Scalar(Low_H, Low_S, Low_V), cv::Scalar(High_H, High_S, High_V), mask);

		cv::Moments mu = moments(mask, false);

		//ターゲットの色を見つけたときのみ実行
		if (!mu.m00 == 0) {
			//ピクセル座標取得(ROIの座標分ずらす必要がある)
			cv::Point2d mc = cv::Point2d(mu.m10 / mu.m00 + ROIPos.x, mu.m01 / mu.m00 + ROIPos.y);
			//				cv::Point2d mc = cv::Point2d(mu.m10 / mu.m00, mu.m01 / mu.m00);

			//デプス取得
			depth_target = depth_data.get_distance((int)mc.x, (int)mc.y);

			//オフセットを初回のみ取得
			if (!offset_depth) {
				zero_depth = depth_target;
				offset_depth = true;
			}

			//過去データを一つずつずらして更新
			for (int j = 1; j < FRAMERATE * DRAW_WID_TIME; j++) past_data[FRAMERATE * DRAW_WID_TIME - j] = past_data[FRAMERATE * DRAW_WID_TIME - j - 1];
			past_data[0] = depth_target - zero_depth;
			//平均
			ave = average(past_data);

			for (int j = 1; j < FRAMERATE * DRAW_WID_TIME; j++) past_data_ave[FRAMERATE * DRAW_WID_TIME - j] = past_data_ave[FRAMERATE * DRAW_WID_TIME - j - 1];
			past_data_ave[0] = ave;

			//データの更新
			if (idx > DATA_SIZE) break;
			auto end = std::chrono::system_clock::now();
			Now_time[idx] = end - start;
			PointData[idx].x = mc.x;
			PointData[idx].y = mc.y;
			PointData[idx].z = ave;
			idx++;
			//			printf("(%d, %d), %f, %f \n", past_data[0], ave);
//				printf("%f \n", ave);

			//circle(image_color, mc, 4, cv::Scalar(0, 0, 200), 2, 4);

			//左に黄色いバーを描画
			cv::rectangle(image_color, cv::Point(20, height_color), cv::Point(60, height_color - (int)(ave * DRAW_SCALE)), cv::Scalar(0, 200, 200), -1, 16);

			//白い線で，折れ線を描画
			draw_graph(image_color, past_data_ave);
			for (int i = 1; i < 11; i++) {
				int wid = 7;
				if (i % 2 == 0) wid = 10;
				if (i % 5 == 0)wid = 20;
				cv::line(image_color, cv::Point(0, height_color - i * DRAW_SCALE / 1000), cv::Point(wid, height_color - i * DRAW_SCALE / 1000), cv::Scalar(200, 200, 200), 2);
			}//数値を表示
			cv::putText(image_color, float_to_string(ave * 1000, 1) + "mm", ROIPos, cv::FONT_HERSHEY_SIMPLEX, 1.0, cv::Scalar(255, 255, 255),2);

			//ROIの更新
			ROIPos.x = mc.x - ROI_SIZE / 2;
			ROIPos.y = mc.y - ROI_SIZE / 2;
			if (ROIPos.x < 0) ROIPos.x = 0;
			if (ROIPos.y < 0) ROIPos.y = 0;
		}
		else {
			float focal[] = { 0,0 ,0 };
		}

		//ROIに画像を描画
		if (image_flg) {
//			cv::Mat roi = image_color(super_rect);
//			cv::imshow("roiroi", superimpose_img);
//			superimpose_img.copyTo(roi);

			overlayImage(&image_color, &superimpose_img, cv::Point(super_x, super_y));

		}
		//表示用に半分サイズにする
//		resize(mask, show_mask, cv::Size(), 0.5, 0.5);
		resize(image_color, image_color, cv::Size(), PICTURE_SCALE, PICTURE_SCALE);
//		resize(image_depth, show_depth, cv::Size(), 0.5, 0.5);

		// Update the window with new data
//		cv::imshow(window_name_color, image_color);
		cv::imshow("Color Image", image_color);
		resize(image_color_hsv(roi_rect), image_roi_hsv, cv::Size(), 3, 3);
		cv::imshow("roi", image_roi_hsv);

		cv::imshow("mask", mask);

		if (!window_init) {
//			cv::moveWindow(window_name_color, 50, 50);
			cv::moveWindow("depth", 50, 50);
			cv::moveWindow("roi", 50 + width_depth*0.75, 50);
			cv::moveWindow("mask", 50 + width_depth*0.75, 200);
			window_init = true;
		}
		cv::setMouseCallback("Color Image", mouseCallback);

		int c = cv::waitKey(1);

		 //スペースキー ファイル保存
		if (c == 32)
		{			

			int btnid = MessageBox(NULL, L"output.csvを上書きしますか？", L"質問", MB_YESNOCANCEL | MB_ICONWARNING);
			if (btnid == IDYES) {
				
				ofstream ofs("output.csv");  // ファイルパスを指定する
				for (int k = 0; k < idx; k++) ofs << Now_time[k].count() << ", " << PointData[k].x << ", " << PointData[k].y << ", " << PointData[k].z << endl;
				
				idx = 0;
				auto start = std::chrono::system_clock::now();
			}//break;
		}
 
		if (c == 'i') image_flg = !image_flg;
		//ESCキー　終了
		if (c == 27) exit(0);
	
	}
	th_a.join();
	return EXIT_SUCCESS;

}

catch (const rs2::error & e)
{
	std::cerr << "RealSense error calling " << e.get_failed_function() << "(" << e.get_failed_args() << "):\n    " << e.what() << std::endl;
	return EXIT_FAILURE;
}
catch (const std::exception & e)
{
	std::cerr << e.what() << std::endl;
	return EXIT_FAILURE;
}


// プログラムの実行: Ctrl + F5 または [デバッグ] > [デバッグなしで開始] メニュー
// プログラムのデバッグ: F5 または [デバッグ] > [デバッグの開始] メニュー

// 作業を開始するためのヒント: 
//    1. ソリューション エクスプローラー ウィンドウを使用してファイルを追加/管理します 
//   2. チーム エクスプローラー ウィンドウを使用してソース管理に接続します
//   3. 出力ウィンドウを使用して、ビルド出力とその他のメッセージを表示します
//   4. エラー一覧ウィンドウを使用してエラーを表示します
//   5. [プロジェクト] > [新しい項目の追加] と移動して新しいコード ファイルを作成するか、[プロジェクト] > [既存の項目の追加] と移動して既存のコード ファイルをプロジェクトに追加します
//   6. 後ほどこのプロジェクトを再び開く場合、[ファイル] > [開く] > [プロジェクト] と移動して .sln ファイルを選択します
