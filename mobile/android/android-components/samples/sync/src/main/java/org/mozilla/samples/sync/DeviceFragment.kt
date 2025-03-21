/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.samples.sync

import android.content.Context
import android.os.Bundle
import android.view.LayoutInflater
import android.view.View
import android.view.ViewGroup
import androidx.fragment.app.Fragment
import androidx.recyclerview.widget.LinearLayoutManager
import androidx.recyclerview.widget.RecyclerView
import mozilla.components.concept.sync.Device

/**
 * A fragment representing a list of Items.
 * Activities containing this fragment MUST implement the
 * [DeviceFragment.OnDeviceListInteractionListener] interface.
 */
class DeviceFragment : Fragment() {
    private val adapter = DeviceRecyclerViewAdapter()

    override fun onCreateView(
        inflater: LayoutInflater,
        container: ViewGroup?,
        savedInstanceState: Bundle?,
    ): View? {
        val view = inflater.inflate(R.layout.fragment_device_list, container, false)

        // Set the adapter
        if (view is RecyclerView) {
            with(view) {
                layoutManager = LinearLayoutManager(context)
                adapter = this@DeviceFragment.adapter
            }
        }
        return view
    }

    override fun onAttach(context: Context) {
        super.onAttach(context)
        require(context is OnDeviceListInteractionListener) {
            "$context must implement OnDeviceListInteractionListener"
        }
        adapter.onDeviceClickedListener = context
    }

    /**
     * Updates the list of devices.
     */
    fun updateDevices(devices: List<Device>) {
        adapter.submitList(devices)
    }

    /**
     * This interface must be implemented by activities that contain this
     * fragment to allow an interaction in this fragment to be communicated
     * to the activity and potentially other fragments contained in that
     * activity.
     *
     *
     * See the Android Training lesson
     * [Communicating with Other Fragments](http://developer.android.com/training/basics/fragments/communicating.html)
     * for more information.
     */
    interface OnDeviceListInteractionListener {
        fun onDeviceInteraction(item: Device)
    }
}
