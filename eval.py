import os
import cv2
import numpy as np
from tqdm import tqdm

import matplotlib.pyplot as plt

def calculate_mse(image1, image2):
    # convert to fp32 to avoid overflow
    image1 = image1.astype(np.float32) / 255.0
    image2 = image2.astype(np.float32) / 255.0
    mse = np.mean((image1 - image2) ** 2)
    return mse

def calculate_mape(image1, image2):
    # convert to fp32 to avoid overflow
    image1 = image1.astype(np.float32) / 255.0
    image2 = image2.astype(np.float32) / 255.0
    divisor = np.maximum(image1, image2)
    divisor = np.maximum(divisor, 0.001)
    mape = np.mean(np.abs(image1 - image2) / divisor)
    return mape

def plot_mse_curves(baseline, ours, references):
    mse_values_baseline = []
    mse_values_ours = []
    for jpeg_image, reference_image in tqdm(zip(baseline, references)):
        mse = calculate_mse(jpeg_image, reference_image)
        mse_values_baseline.append(mse)
    for jpeg_image, reference_image in tqdm(zip(ours, references)):
        mse = calculate_mse(jpeg_image, reference_image)
        mse_values_ours.append(mse)
    plt.close()
    plt.plot(mse_values_baseline, label='Baseline')
    plt.plot(mse_values_ours, label='Ours')
    plt.xlabel('Frame number')
    plt.ylabel('MSE')
    plt.legend()
    # export to opencv
    fig = plt.gcf()
    fig.canvas.draw()
    image_from_plot = np.frombuffer(fig.canvas.buffer_rgba(), dtype='uint8')
    image_from_plot = image_from_plot.reshape(fig.canvas.get_width_height()[::-1] + (4,))
    # drop alpha channel
    image_from_plot = image_from_plot[:, :, :3]

    # 将 RGB 图像转换为 BGR 格式
    cv_img = cv2.cvtColor(image_from_plot, cv2.COLOR_RGB2BGR)
    return cv_img

def plot_mape_curves(baseline, ours, references):
    mape_values_baseline = []
    mape_values_ours = []
    for jpeg_image, reference_image in tqdm(zip(baseline, references)):
        mape = calculate_mape(jpeg_image, reference_image)
        mape_values_baseline.append(mape)
    for jpeg_image, reference_image in tqdm(zip(ours, references)):
        mape = calculate_mape(jpeg_image, reference_image)
        mape_values_ours.append(mape)
    plt.close()
    plt.plot(mape_values_baseline, label='Baseline')
    plt.plot(mape_values_ours, label='Ours')
    plt.xlabel('Frame number')
    plt.ylabel('MAPE')
    plt.legend()
    # export to opencv
    fig = plt.gcf()
    fig.canvas.draw()
    image_from_plot = np.frombuffer(fig.canvas.buffer_rgba(), dtype='uint8')
    image_from_plot = image_from_plot.reshape(fig.canvas.get_width_height()[::-1] + (4,))
    # drop alpha channel
    image_from_plot = image_from_plot[:, :, :3]

    # 将 RGB 图像转换为 BGR 格式
    cv_img = cv2.cvtColor(image_from_plot, cv2.COLOR_RGB2BGR)
    return cv_img


track_name = "diff-highlights"

# Path to the directories containing the JPEG files and references
track_evals_base_dir = track_name + '/track_evals_base'
track_evals_sg_dir = track_name + '/track_evals_sg'
track_refs_dir = track_name + '/track_refs'
# Get the list of JPEG files and references
baseline_images = [os.path.join(track_evals_base_dir, file) for file in os.listdir(track_evals_base_dir) if file.endswith('.jpeg')]
ours_images = [os.path.join(track_evals_sg_dir, file) for file in os.listdir(track_evals_sg_dir) if file.endswith('.jpeg')]
references = [os.path.join(track_refs_dir, file) for file in os.listdir(track_refs_dir) if file.endswith('.jpeg')]

# Sort the lists according to their suffix numbers
baseline_images.sort(key=lambda x: int(x.split('_')[-1].split('.')[0]))
ours_images.sort(key=lambda x: int(x.split('_')[-1].split('.')[0]))
references.sort(key=lambda x: int(x.split('_')[-1].split('.')[0]))

# Load images to memory in parallel and use tqdm to display a progress bar
baseline_images = [cv2.imread(file) for file in tqdm(baseline_images)]
ours_images = [cv2.imread(file) for file in tqdm(ours_images)]
references  = [cv2.imread(file) for file in tqdm(references)]

# Plot the MSE curves
mse_plot_img = plot_mse_curves(baseline_images, ours_images, references)
# Plot the MAPE curves
mape_plot_img = plot_mape_curves(baseline_images, ours_images, references)

# Compose an animation of the reference, baseline, and our results
fourcc = cv2.VideoWriter_fourcc(*'XVID')
video_size = (1920, 1080)
out = cv2.VideoWriter(track_name + '.avi', fourcc, 10.0, video_size)

# Write the MSE plot image to the video (first 30 frames)
# Resize to video size
scale = min(video_size[0] / mse_plot_img.shape[1], video_size[1] / mse_plot_img.shape[0])
mse_plot_img_resized = cv2.resize(mse_plot_img, (0, 0), fx=scale, fy=scale)
# Pad the image to the video size
pad_sizes = (video_size[1] - mse_plot_img_resized.shape[0], video_size[0] - mse_plot_img_resized.shape[1])
mse_plot_img_resized = np.pad(mse_plot_img_resized, ((pad_sizes[0]//2, pad_sizes[0] - pad_sizes[0]//2), (pad_sizes[1]//2, pad_sizes[1] - pad_sizes[1]//2), (0, 0)), mode='constant')
for i in range(30):
    out.write(mse_plot_img_resized)

# Write the MAPE plot image to the video (first 30 frames)
# Resize to video size
scale = min(video_size[0] / mape_plot_img.shape[1], video_size[1] / mape_plot_img.shape[0])
mape_plot_img_resized = cv2.resize(mape_plot_img, (0, 0), fx=scale, fy=scale)
# Pad the image to the video size
pad_sizes = (video_size[1] - mape_plot_img_resized.shape[0], video_size[0] - mape_plot_img_resized.shape[1])
mape_plot_img_resized = np.pad(mape_plot_img_resized, ((pad_sizes[0]//2, pad_sizes[0] - pad_sizes[0]//2), (pad_sizes[1]//2, pad_sizes[1] - pad_sizes[1]//2), (0, 0)), mode='constant')
for i in range(30):
    out.write(mape_plot_img_resized)

# make a progress bar
for baseline, ours, reference in tqdm(zip(baseline_images, ours_images, references)):
    height = baseline.shape[0]
    width  = baseline.shape[1]
    # Crop the center of the images to let them fit in a 1920x1080 frame in a 3x1 layout
    # 640 = 1920 / 3, take the center 640xheight region
    baseline = baseline[:, width // 2 - 320: width // 2 + 320, :]
    ours = ours[:, width // 2 - 320: width // 2 + 320, :]
    reference = reference[:, width // 2 - 320: width // 2 + 320, :]
    # Pad the images to a height of video size
    if height < video_size[1]:
        baseline = np.pad(baseline, ((0, video_size[1] - height), (0, 0), (0, 0)), mode='constant')
        ours = np.pad(ours, ((0, video_size[1] - height), (0, 0), (0, 0)), mode='constant')
        reference = np.pad(reference, ((0, video_size[1] - height), (0, 0), (0, 0)), mode='constant')
    else: 
        baseline = baseline[:video_size[1], :, :]
        ours = ours[:video_size[1], :, :]
        reference = reference[:video_size[1], :, :]
    combined = np.concatenate((reference, baseline, ours), axis=1)

    # Display the labels under each image
    font = cv2.FONT_HERSHEY_SIMPLEX
    cv2.putText(combined, 'Reference', (50, 1000), font, 2, (255, 255, 255), 2, cv2.LINE_AA)
    cv2.putText(combined, 'Baseline', (50 + 640, 1000), font, 2, (255, 255, 255), 2, cv2.LINE_AA)
    cv2.putText(combined, 'Ours', (50 + 640 * 2, 1000), font, 2, (255, 255, 255), 2, cv2.LINE_AA)

    # Compute and display MSE values
    mse_baseline = calculate_mse(baseline, reference)
    mse_ours = calculate_mse(ours, reference)
    cv2.putText(combined, 'MSE: {:.4f}'.format(mse_baseline), (50 + 640, 50), font, 2, (255, 255, 255), 2, cv2.LINE_AA)
    cv2.putText(combined, 'MSE: {:.4f}'.format(mse_ours), (50 + 640 * 2, 50), font, 2, (255, 255, 255), 2, cv2.LINE_AA)

    # Compute and display MAPE values just below the MSE values
    mape_baseline = calculate_mape(baseline, reference)
    mape_ours = calculate_mape(ours, reference)
    cv2.putText(combined, 'MAPE: {:.4f}'.format(mape_baseline), (50 + 640, 100), font, 2, (255, 255, 255), 2, cv2.LINE_AA)
    cv2.putText(combined, 'MAPE: {:.4f}'.format(mape_ours), (50 + 640 * 2, 100), font, 2, (255, 255, 255), 2, cv2.LINE_AA)


    # Check the shape of the combined image
    if combined.shape[0] != video_size[1] or combined.shape[1] != video_size[0]:
        print('Error: combined image has shape {}'.format(combined.shape))
        break
    out.write(combined)

# Visualize MSE comparison

for baseline, ours, reference in tqdm(zip(baseline_images, ours_images, references)):
    height = baseline.shape[0]
    width  = baseline.shape[1]
    # Crop the center of the images to let them fit in a 1920x1080 frame in a 3x1 layout
    # 640 = 1920 / 3, take the center 640xheight region
    baseline = baseline[:, width // 2 - 320: width // 2 + 320, :]
    ours = ours[:, width // 2 - 320: width // 2 + 320, :]
    reference = reference[:, width // 2 - 320: width // 2 + 320, :]
    # Pad the images to a height of video size
    if height < video_size[1]:
        baseline = np.pad(baseline, ((0, video_size[1] - height), (0, 0), (0, 0)), mode='constant')
        ours = np.pad(ours, ((0, video_size[1] - height), (0, 0), (0, 0)), mode='constant')
        reference = np.pad(reference, ((0, video_size[1] - height), (0, 0), (0, 0)), mode='constant')
    else: 
        baseline = baseline[:video_size[1], :, :]
        ours = ours[:video_size[1], :, :]
        reference = reference[:video_size[1], :, :]
    
    # Compute the difference images
    diff_baseline = cv2.absdiff(baseline, reference)
    diff_ours = cv2.absdiff(ours, reference)
    # Heatmap the difference images
    diff_baseline = cv2.applyColorMap(diff_baseline, cv2.COLORMAP_JET)
    diff_ours = cv2.applyColorMap(diff_ours, cv2.COLORMAP_JET)
    
    combined = np.concatenate((reference, diff_baseline, diff_ours), axis=1)

    # Display the labels under each image
    font = cv2.FONT_HERSHEY_SIMPLEX
    cv2.putText(combined, 'Reference', (50, 1000), font, 2, (255, 255, 255), 2, cv2.LINE_AA)
    cv2.putText(combined, 'Baseline', (50 + 640, 1000), font, 2, (255, 255, 255), 2, cv2.LINE_AA)
    cv2.putText(combined, 'Ours', (50 + 640 * 2, 1000), font, 2, (255, 255, 255), 2, cv2.LINE_AA)

    # Compute and display the MSE values
    mse_baseline = calculate_mse(baseline, reference)
    mse_ours = calculate_mse(ours, reference)
    cv2.putText(combined, 'MSE: {:.4f}'.format(mse_baseline), (50 + 640, 50), font, 2, (255, 255, 255), 2, cv2.LINE_AA)
    cv2.putText(combined, 'MSE: {:.4f}'.format(mse_ours), (50 + 640 * 2, 50), font, 2, (255, 255, 255), 2, cv2.LINE_AA)

    # Check the shape of the combined image
    if combined.shape[0] != video_size[1] or combined.shape[1] != video_size[0]:
        print('Error: combined image has shape {}'.format(combined.shape))
        break
    out.write(combined)

# Visualize MAPE comparison

for baseline, ours, reference in tqdm(zip(baseline_images, ours_images, references)):
    height = baseline.shape[0]
    width  = baseline.shape[1]
    # Crop the center of the images to let them fit in a 1920x1080 frame in a 3x1 layout
    # 640 = 1920 / 3, take the center 640xheight region
    baseline = baseline[:, width // 2 - 320: width // 2 + 320, :]
    ours = ours[:, width // 2 - 320: width // 2 + 320, :]
    reference = reference[:, width // 2 - 320: width // 2 + 320, :]
    # Pad the images to a height of video size
    if height < video_size[1]:
        baseline = np.pad(baseline, ((0, video_size[1] - height), (0, 0), (0, 0)), mode='constant')
        ours = np.pad(ours, ((0, video_size[1] - height), (0, 0), (0, 0)), mode='constant')
        reference = np.pad(reference, ((0, video_size[1] - height), (0, 0), (0, 0)), mode='constant')
    else: 
        baseline = baseline[:video_size[1], :, :]
        ours = ours[:video_size[1], :, :]
        reference = reference[:video_size[1], :, :]
    
    # Compute the difference images
    diff_baseline = cv2.absdiff(baseline, reference)
    diff_ours = cv2.absdiff(ours, reference)
    # Heatmap the difference images
    diff_baseline = cv2.applyColorMap(diff_baseline, cv2.COLORMAP_JET)
    diff_ours = cv2.applyColorMap(diff_ours, cv2.COLORMAP_JET)
    
    combined = np.concatenate((reference, diff_baseline, diff_ours), axis=1)

    # Display the labels under each image
    font = cv2.FONT_HERSHEY_SIMPLEX
    cv2.putText(combined, 'Reference', (50, 1000), font, 2, (255, 255, 255), 2, cv2.LINE_AA)
    cv2.putText(combined, 'Baseline', (50 + 640, 1000), font, 2, (255, 255, 255), 2, cv2.LINE_AA)
    cv2.putText(combined, 'Ours', (50 + 640 * 2, 1000), font, 2, (255, 255, 255), 2, cv2.LINE_AA)

    # Compute and display the MAPE values
    mape_baseline = calculate_mape(baseline, reference)
    mape_ours = calculate_mape(ours, reference)
    cv2.putText(combined, 'MAPE: {:.4f}'.format(mape_baseline), (50 + 640, 50), font, 2, (255, 255, 255), 2, cv2.LINE_AA)
    cv2.putText(combined, 'MAPE: {:.4f}'.format(mape_ours), (50 + 640 * 2, 50), font, 2, (255, 255, 255), 2, cv2.LINE_AA)

    # Check the shape of the combined image
    if combined.shape[0] != video_size[1] or combined.shape[1] != video_size[0]:
        print('Error: combined image has shape {}'.format(combined.shape))
        break
    out.write(combined)


out.release()
