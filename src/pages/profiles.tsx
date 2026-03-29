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

  {/*Refresh the page to get all the keyboard configuration that where saved*/}
  useEffect(() => {
    refresh()
  }, [])

  return (
    <div className="p-8 bg-white">
      <h1 className="text-3xl font-bold">Profiles</h1>
      <p className="text-gray-600 mt-2">Different profiles management</p>


      <div style={{ padding: 20 }}>

      <h2>Keyboard Profile Test</h2>

      {/* INPUT */}
      <div>
        <label>Profile name :</label>
        <input
          value={name}
          onChange={e => setName(e.target.value)}
          placeholder="gaming / work / fps"
        />

        <button onClick={() => save(name)}>
          Save profile
        </button>

        <button onClick={refresh}>
          Refresh
        </button>
      </div>
    </div>



     <hr />


      {/* PROFILE LIST */}
      <h3>Saved Profiles</h3>

      <p>Selected profile: {selectedProfile?.name ?? "none"}</p>

      {profiles.length === 0 && (
        <p>No profile saved</p>
      )}

      {profiles.map(profile => (

  <div
    key={profile.name}
    style={{
      border: selectedProfile?.name === profile.name
        ? "2px solid green"
        : "1px solid gray",
      padding: 10,
      marginBottom: 10
    }}
  >

    <strong>{profile.name}</strong>

    {selectedProfile?.name === profile.name && (
      <span style={{ marginLeft: 10 }}>✓ Selected</span>
    )}

    <div style={{ marginTop: 5 }}>

      <button className="bg-blue-500 hover:bg-blue-700 text-white font-bold py-2 px-4 rounded" onClick={() => selectProfile(profile.name)}>
        Select
      </button>

      <button className="bg-red-500 hover:bg-red-700 text-white font-bold py-2 px-4 rounded" onClick={() => remove(profile.name)}>
        Delete
      </button>

    </div>

  </div>
  

))}

    </div> 

  )}
