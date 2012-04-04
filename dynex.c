/*
 * XU example for uvcvideo driver.
 * All parameters based on /usr/share/uvcdynctrl/data/046d/logitech.xml
 * and tested with Logitech, Inc. QuickCam Pro for Notebooks (046d:0991)
 * Use it on your own risk, it can damage you device.
 *
 * Copyright (C) 2012
 * Oleksij Rempel <bug-track@fisher-privat.net>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * Description:
 * Most Logitech Cams support so called "RightLight Technology"
 * It is software based multi field mesuring and adopted exposure
 * settings.
 * The thecnology works as fallow:
 * the image on the cam will splitted to 8 fields, each field has two parts.
 *  -----------------  To get the light intensity of one field,
 *  |   |   |   |   |  host should SET the adress of field and then 
 *  |  c4   |  c5   |  GET the result. The result has two bytes,
 *  -----------------  first byte is the left part of the field,
 *  |   |   |   |   |  sesond byte is the right part of the field.
 *  |  c6   |  c7   |  
 *  -----------------
 *  |   |   |   |   |
 *  |  c8   |  c9   |
 *  -----------------
 *  |   |   |   |   |
 *  |  ca   |  cb   |
 *  -----------------
 */

#include <unistd.h>
#include <stdint.h>
#include <linux/uvcvideo.h>
#include <linux/videodev2.h>
#include <linux/usb/video.h>
#include <assert.h>
#include <stdio.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <string.h>
#include <errno.h>
#include <stdlib.h>

#define DEVVIDEO "/dev/video0"

#define EXPOSURE_STEP 5
#define MIN_EXPOSURE 50
#define MAX_EXPOSURE 300
#define NAME1 "point"
#define NAME2 "point_ret"
#define UVC_UID_LOGITECH_USER_HW_CONTROL 10

/* This is the GUID of XU repsoinble for mesuring
 * 82066163-7050-ab49-b8cc-b3855e8d2252.
 */
#define UVC_XU_GUID \
  {0x82, 0x06, 0x61, 0x63, 0x70, 0x50, 0xab, 0x49, \
   0xb8, 0xcc, 0xb3, 0x85, 0x5e, 0x8d, 0x22, 0x52}

/* With CS 5 we set the adress of field
 * On CS 6 we get results.
 */
#define XU_CS_MESURE_FIELD 5
#define XU_CS_MESURE_FIELD_RET 6

/* for some unknown reason we need to send two bytes:
 * filed number and 0x02.*/
#define MESURE_FIELDS \

const uint16_t fields[8] = {0xc402, 0xc502, 0xc602, 0xc702, 0xc802, 0xc902, 0xca02, 0xcb02};

struct dyn_exposure {
  int configured;
 // uint16_t fields[8] = MESURE_FIELDS;
//  uint16_t fields[8];
  uint8_t results[16];
  int fd;
  struct uvc_xu_control_query xu_s;
  struct uvc_xu_control_query xu_g;
};

int xioctl(int fd, int request, void *arg) {

  int ret = ioctl(fd, request, arg);
  if (ret == -1) {
    /* check the errno, it can be standart ioctl error or
     * errno provided by uvcvideo. For possible errors see:
     * linux/drivers/media/video/uvc/uvc_ctrl.c: uvc_xu_ctrl_query
     * and linux/include/asm-generic/errno.h */
    ret =  errno;
    printf("filed with errno: %d\n", ret);
  }
  return ret;
}

void set_exposure(int fd, int exp) {
  struct v4l2_control ctrl;

  /* check if we use manual exposure
   * if not set it. 
   */
  ctrl.id = V4L2_CID_EXPOSURE_AUTO;
  ctrl.value = 0;
  xioctl(fd, VIDIOC_G_CTRL, &ctrl);
  if (ctrl.value != 1) {
    ctrl.value = 1;
    xioctl(fd, VIDIOC_S_CTRL, &ctrl);
    printf ("Setting manual exposure\n", ctrl.value);
  }

  ctrl.id = V4L2_CID_EXPOSURE_ABSOLUTE;
  ctrl.value = 0; 
  xioctl(fd, VIDIOC_G_CTRL, &ctrl);
  printf (" --- %02x\n", ctrl.value);
  /* */
  if (exp < 0) {
    ctrl.value -= EXPOSURE_STEP;
    if (ctrl.value < MIN_EXPOSURE)
      ctrl.value = MIN_EXPOSURE;
    xioctl(fd, VIDIOC_S_CTRL, &ctrl);
  } else if (exp > 0) {
    ctrl.value += EXPOSURE_STEP;
    if (ctrl.value < MAX_EXPOSURE)
      ctrl.value = MAX_EXPOSURE;
    xioctl(fd, VIDIOC_S_CTRL, &ctrl);
  }
}

int map_xu(int fd)
{
  struct uvc_xu_control_mapping xu_map = { 0 };
  int ret, i;
  uint8_t uuid[16] = UVC_XU_GUID;
  /* same for all */
  memcpy(xu_map.entity, uuid, sizeof(xu_map.entity));
  xu_map.v4l2_type = V4L2_CTRL_TYPE_INTEGER;
  xu_map.offset = 0;
  xu_map.data_type = UVC_CTRL_DATA_TYPE_UNSIGNED;

  /* field setter */
  strncpy((char *)xu_map.name, NAME1, sizeof(xu_map.name));
  xu_map.id = V4L2_CID_PRIVATE_BASE;
  xu_map.size = 2;
  xu_map.selector = XU_CS_MESURE_FIELD;
  ret = xioctl(fd, UVCIOC_CTRL_MAP, &xu_map);
  if (ret != 0 && ret != EEXIST) {
    printf ("%s:%i oops, some thing wrong(%i)\n", __func__, __LINE__, ret);
    return ret;
  }

  /* field getter */
  strncpy((char *)xu_map.name, NAME2, sizeof(xu_map.name));
  xu_map.id = V4L2_CID_PRIVATE_BASE + 1;
  xu_map.size = 2;
  xu_map.selector = XU_CS_MESURE_FIELD_RET;
  ret = xioctl(fd, UVCIOC_CTRL_MAP, &xu_map);
  if (ret != 0 && ret != EEXIST) {
    printf ("%s:%i oops, some thing wrong(%i)\n", __func__, __LINE__, ret);
    return ret;
  }
}

void check_point (int fd)
{
  struct v4l2_control ctrl;
  ctrl.id = V4L2_CID_PRIVATE_BASE + 1;
  ctrl.value = 0;
  xioctl(fd, VIDIOC_G_CTRL, &ctrl);
  printf (" --- %x\n", ctrl.value);
}

void init_xu(struct dyn_exposure *priv)
{
  /* prepare set_ struct */
  priv->xu_s.unit = UVC_UID_LOGITECH_USER_HW_CONTROL;
  priv->xu_s.selector = XU_CS_MESURE_FIELD;
  priv->xu_s.query = UVC_SET_CUR;
  priv->xu_s.size = 2;

  /* prepare get struct */
  priv->xu_g.unit = UVC_UID_LOGITECH_USER_HW_CONTROL;
  priv->xu_g.selector = XU_CS_MESURE_FIELD_RET;
  priv->xu_g.query = UVC_GET_CUR;
  priv->xu_g.size = 2;

}

void get_xu_fileds(struct dyn_exposure *priv)
{
  int i8, i16;
  uint16_t *ptr;
  i16 = 0;
  for (i8 = 0; i8 < 8; i8++)
  {
    printf ("%i .. 0x%02x\n", i8, fields[i8]);
    priv->xu_g.data = (unsigned char*)&fields[i8];
    xioctl(priv->fd, UVCIOC_CTRL_QUERY, (void *)&priv->xu_s);
    
    priv->xu_g.data = (unsigned char*)&priv->results[i16];
    xioctl(priv->fd, UVCIOC_CTRL_QUERY, (void *)&priv->xu_g);
    i16 += 2;
  }
}

int main(void) {

  int fd = open(DEVVIDEO, O_RDWR);
  assert(fd > 0);
  {
    int ret, i, t, z;
    /* adresses of mesure points */
    uint8_t points[8] = {0xc4, 0xc5, 0xc6, 0xc7, 0xc8, 0xc9, 0xca, 0xcb};
    uint8_t set[2];
    uint8_t mesure[16];

    struct dyn_exposure priv;
    priv.fd = fd;
    init_xu (&priv);
    get_xu_fileds (&priv); 

  //  map_xu(fd);


return;
    t = 0;
    int r = 0;
    for (i = 0; i < 4; i++)
    {
      for (z = 0; z < 4; z++)
      {
        if (mesure[t] < 0x30)
          r -= 1; // underexposed
        else if (mesure[t] > 0x60)
          r += 2; // over exposed
        else
          r += 1; // ok
          
        
        printf("| 0x%02x ", mesure[t]);
        t++;
      }
      printf("|\n");
    }
      if (r > 18) {
        printf ("over exposed\n");
        set_exposure (fd, 1);
      } else if (r < 10) {
        printf ("underexposed \n");
        set_exposure (fd, -1);
      } else
        printf ("ok");

  }
  close(fd);
  return 0;
}
