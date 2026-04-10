import { useState, useEffect } from "react"
import { useProfileStore } from "@/stores/profileStore"


export default function Profiles() {
  const [name, setName] = useState("")

  const profiles = useProfileStore(state => state.profiles)
  const refresh = useProfileStore(state => state.refresh)
  const save = useProfileStore(state => state.save)
  const remove = useProfileStore(state => state.remove)
  const selectedProfile = useProfileStore(state => state.selectedProfile)
  const selectProfile = useProfileStore(state => state.selectProfile)

  {/*Refresh the page to get all the keyboard configuration that where saved*/ }
  useEffect(() => {
    refresh()
  }, [])

  return (
    <div className="p-8 gray-100">
      <h1 className="flex items-center text-3xl font-bold border border-gray-300 rounded-md justify-center px-6">Profiles</h1>
      <p className="text-gray-600 mt-2">Different profiles management</p>


      <div style={{ padding: 20 }}>

        <h2>Keyboard Profile Test</h2>

        {/* INPUT */}
        <div className="flex flex-wrap items-center gap-3">
          <label className="whitespace-nowrap">Profile name :</label>
          <input
            className="border rounded px-2 py-1"
            value={name}
            onChange={e => setName(e.target.value)}
            placeholder="gaming / work / fps"
          />

          <button
            className="bg-blue-500 hover:bg-blue-700 text-white font-bold py-2 px-4 rounded"
            onClick={() => save(name)}
          >
            Save profile
          </button>

          <button
            className="bg-gray-200 hover:bg-gray-300 text-gray-800 font-bold py-2 px-4 rounded"
            onClick={refresh}
          >
             Refresh
          </button>
        </div>
      </div>



      <hr />


      {/* PROFILE LIST */}
      <h3 className="flex items-center border border-gray-300 rounded-md justify-center px-6 font-bold text-2xl mt-4">Saved Profiles</h3>

      <p className="mt-2">
        Selected profile: <strong>{selectedProfile?.name ?? "none"}</strong>
      </p>

      {profiles.length === 0 && (
        <p>No profile saved</p>
      )}

      <div className="flex flex-wrap gap-4 mt-4">
        {profiles.map(profile => (
          <div
            key={profile.name}
            className="border rounded p-3 flex flex-col gap-2 w-full sm:w-1/2 md:w-1/3 xl:w-1/4 2xl:w-1/5 max-w-[220px]"
          >
            <div className="flex items-center justify-between gap-2">
              <strong>{profile.name}</strong>
              {selectedProfile?.name === profile.name && (
                <span className="text-sm text-green-600">✓ Selected</span>
              )}
            </div>

            <div className="flex gap-2">
              <button
                className="bg-blue-500 hover:bg-blue-700 text-white font-bold py-1 px-3 rounded"
                onClick={() => selectProfile(profile.name)}
              >
                Select
              </button>

              <button
                className="bg-red-500 hover:bg-red-700 text-white font-bold py-1 px-3 rounded"
                onClick={() => remove(profile.name)}
              >
                Delete
              </button>
            </div>
          </div>
        ))}
      </div>

    </div>

  )
}
