# -*- coding: utf-8 -*-
# Generated by Django 1.11.28 on 2020-02-24 06:32
from __future__ import unicode_literals

from django.conf import settings
from django.db import migrations, models
import django.db.models.deletion


class Migration(migrations.Migration):

    dependencies = [
        ('net', '0001_initial'),
    ]

    operations = [
        migrations.AlterField(
            model_name='album',
            name='user',
            field=models.ForeignKey(null=True, on_delete=django.db.models.deletion.SET_NULL, related_name='albums', to=settings.AUTH_USER_MODEL),
        ),
        migrations.AlterField(
            model_name='calibration',
            name='raw_tan',
            field=models.ForeignKey(null=True, on_delete=django.db.models.deletion.SET_NULL, related_name='calibrations_raw', to='net.TanWCS'),
        ),
        migrations.AlterField(
            model_name='calibration',
            name='sip',
            field=models.ForeignKey(null=True, on_delete=django.db.models.deletion.SET_NULL, to='net.SipWCS'),
        ),
        migrations.AlterField(
            model_name='calibration',
            name='sky_location',
            field=models.ForeignKey(null=True, on_delete=django.db.models.deletion.SET_NULL, related_name='calibrations', to='net.SkyLocation'),
        ),
        migrations.AlterField(
            model_name='calibration',
            name='tweaked_tan',
            field=models.ForeignKey(null=True, on_delete=django.db.models.deletion.SET_NULL, related_name='calibrations_tweaked', to='net.TanWCS'),
        ),
        migrations.AlterField(
            model_name='commentreceiver',
            name='owner',
            field=models.ForeignKey(null=True, on_delete=django.db.models.deletion.SET_NULL, to=settings.AUTH_USER_MODEL),
        ),
        migrations.AlterField(
            model_name='image',
            name='display_image',
            field=models.ForeignKey(null=True, on_delete=django.db.models.deletion.SET_NULL, related_name='image_display_set', to='net.Image'),
        ),
        migrations.AlterField(
            model_name='image',
            name='thumbnail',
            field=models.ForeignKey(null=True, on_delete=django.db.models.deletion.SET_NULL, related_name='image_thumbnail_set', to='net.Image'),
        ),
        migrations.AlterField(
            model_name='submission',
            name='album',
            field=models.ForeignKey(blank=True, null=True, on_delete=django.db.models.deletion.SET_NULL, to='net.Album'),
        ),
        migrations.AlterField(
            model_name='submission',
            name='disk_file',
            field=models.ForeignKey(null=True, on_delete=django.db.models.deletion.SET_NULL, related_name='submissions', to='net.DiskFile'),
        ),
        migrations.AlterField(
            model_name='submission',
            name='license',
            field=models.ForeignKey(null=True, on_delete=django.db.models.deletion.SET_NULL, to='net.License'),
        ),
        migrations.AlterField(
            model_name='submission',
            name='user',
            field=models.ForeignKey(null=True, on_delete=django.db.models.deletion.SET_NULL, related_name='submissions', to=settings.AUTH_USER_MODEL),
        ),
        migrations.AlterField(
            model_name='taggeduserimage',
            name='tagger',
            field=models.ForeignKey(null=True, on_delete=django.db.models.deletion.SET_NULL, to=settings.AUTH_USER_MODEL),
        ),
        migrations.AlterField(
            model_name='userimage',
            name='license',
            field=models.ForeignKey(null=True, on_delete=django.db.models.deletion.SET_NULL, to='net.License'),
        ),
        migrations.AlterField(
            model_name='userimage',
            name='user',
            field=models.ForeignKey(null=True, on_delete=django.db.models.deletion.SET_NULL, related_name='user_images', to=settings.AUTH_USER_MODEL),
        ),
        migrations.AlterField(
            model_name='userprofile',
            name='default_license',
            field=models.ForeignKey(default=1, on_delete=django.db.models.deletion.SET_DEFAULT, to='net.License'),
        ),
        migrations.AlterField(
            model_name='userprofile',
            name='user',
            field=models.ForeignKey(editable=False, on_delete=django.db.models.deletion.CASCADE, related_name='profile', to=settings.AUTH_USER_MODEL, unique=True),
        ),
    ]