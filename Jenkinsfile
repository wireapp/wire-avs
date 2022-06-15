buildNumber = currentBuild.id
version = null
branchName = null
commitId = null
repoName = null
changelog = ""

pipeline {
    agent none

    options {
        parallelsAlwaysFailFast()
        disableConcurrentBuilds()
    }

    // NOTE: checks every 5 minutes if a new commit occurred after last successful run
    triggers {
        pollSCM 'H/5 * * * *'
    }

    stages {
       stage('Test + Build') {
            parallel {
                stage('Linux') {
                    agent {
                        dockerfile true
                    }
                    steps {
                        script {
                            def vcs = checkout([
                                    $class: 'GitSCM',
                                    changelog: true,
                                    userRemoteConfigs: scm.userRemoteConfigs,
                                    branches: scm.branches,
                                    extensions: scm.extensions + [
                                            [
                                            $class: 'SubmoduleOption',
                                            disableSubmodules: false,
                                            recursiveSubmodules: true,
                                            parentCredentials: true
                                            ]
                                    ]
                            ])

                            branchName = vcs.GIT_BRANCH.tokenize( '/' ).drop( 1 ).join( '/' )
                            commitId = "${vcs.GIT_COMMIT}"[0..6]
                            repoName = vcs.GIT_URL.tokenize( '/' ).last().tokenize( '.' ).first()
                            repoUser = vcs.GIT_URL.tokenize( '/' )[-2]

                            if (params.RELEASE_VERSION == null || params.RELEASE_VERSION == "") {
                                version = "0.0.${buildNumber}"
                            } else {
                                version = "${RELEASE_VERSION}.${buildNumber}"
                            }
                        }

                        // clean
                        sh 'make distclean'
                        sh 'touch src/version/version.c'

                        // build tests
                        sh 'make test AVS_VERSION=' + version
                        // run tests
                        sh './ztest'

                        sh 'make dist_clean'
                        sh 'make zcall AVS_VERSION=' + version
                        sh 'make dist_linux AVS_VERSION=' + version + ' BUILDVERSION=' + version
                        sh 'rm -rf ./build/artifacts'
                        sh 'mkdir -p ./build/artifacts'
                        sh 'cp ./build/dist/linux/avscore.tar.bz2 ./build/artifacts/avs.linux.' + version + '.tar.bz2'
                        sh 'zip -9j ./build/artifacts/zcall_linux_' + version + '.zip ./zcall'

                        archiveArtifacts artifacts: 'build/artifacts/*.zip,build/artifacts/*.bz2', followSymlinks: false
                    }
                }
                stage('macOS') {
                    agent {
                        label 'built-in'
                    }
                    steps {
                        script {
                            def vcs = checkout([
                                    $class: 'GitSCM',
                                    changelog: true,
                                    userRemoteConfigs: scm.userRemoteConfigs,
                                    branches: scm.branches,
                                    extensions: scm.extensions + [
                                            [
                                            $class: 'SubmoduleOption',
                                            disableSubmodules: false,
                                            recursiveSubmodules: true,
                                            parentCredentials: true
                                            ]
                                    ]
                            ])

                            branchName = vcs.GIT_BRANCH.tokenize( '/' ).drop( 1 ).join( '/' )
                            commitId = "${vcs.GIT_COMMIT}"[0..6]
                            repoName = vcs.GIT_URL.tokenize( '/' ).last().tokenize( '.' ).first()
                            repoUser = vcs.GIT_URL.tokenize( '/' )[-2]

                            if (params.RELEASE_VERSION == null || params.RELEASE_VERSION == "") {
                                version = "0.0.${buildNumber}"
                            } else {
                                version = "${RELEASE_VERSION}.${buildNumber}"
                            }
                        }

                        // clean
                        sh 'make distclean'
                        sh 'touch src/version/version.c'

                        // build tests
                        sh 'make test AVS_VERSION=' + version
                        // run tests
                        sh './ztest'

                        // build
                        sh 'mkdir -p ./contrib/webrtc/72.5/lib/wasm-generic'
                        sh 'touch ./contrib/webrtc/72.5/lib/wasm-generic/libwebrtc.a'

                        sh '. ./scripts/android_devenv.sh && . ./scripts/wasm_devenv.sh && make dist_clean'
                        sh '. ./scripts/android_devenv.sh && . ./scripts/wasm_devenv.sh && make zcall AVS_VERSION=' + version
                        sh '. ./scripts/android_devenv.sh && . ./scripts/wasm_devenv.sh && make dist AVS_VERSION=' + version + ' BUILDVERSION=' + version

                        sh 'rm -rf ./build/artifacts'
                        sh 'mkdir -p ./build/artifacts'
                        sh 'cp ./build/dist/osx/avs.framework.zip ./build/artifacts/avs.framework.osx.' + version + '.zip'
                        sh 'cp ./build/dist/ios/avs.xcframework.zip ./build/artifacts/avs.xcframework.zip'
                        sh 'zip -9j ./build/artifacts/avs.android.' + version + '.zip ./build/dist/android/avs.aar'
                        sh 'zip -9j ./build/artifacts/zcall_osx_' + version + '.zip ./zcall'
                        sh 'cp ./build/dist/wasm/wireapp-avs-' + version + '.tgz ./build/artifacts/'
                        // TODO: Add debug artifact
                        sh 'mkdir -p ./osx'
                        sh 'cp ./build/dist/osx/avscore.tar.bz2 ./osx'

                        archiveArtifacts artifacts: 'build/artifacts/*.zip,build/artifacts/*.tgz', followSymlinks: false
                    }
                }
            }
        }
        stage('Prepare changelog') {
            steps {
                script {
                    currentBuild.changeSets.each { set ->
                        set.items.each { entry ->
                            changelog += '- ' + entry.msg + '\n'
                        }
                    }
                }
                echo("Changelog:")
                echo(changelog)
            }
        }
        stage('Tag & create Github release') {
            agent {
                label "linuxbuild"
            }
            when {
                anyOf {
                    expression { return "${branchName}".startsWith('release') || "${version}".startsWith('0.0') }
                }
            }

            steps {
                echo "Tag as ${version}"
                withCredentials([ sshUserPrivateKey( credentialsId: 'avs-github-ssh', keyFileVariable: 'sshPrivateKeyPath' ) ]) {
                    sh(
                        script: """
                            #!/usr/bin/env bash

                            cd "${env.WORKSPACE}"

                            git tag ${version}

                            git \
                                -c core.sshCommand='ssh -i ${sshPrivateKeyPath}' \
                                push \
                                origin ${version}
                        """
                    )
                }
                echo 'Creating release on Github'
                withCredentials([ string( credentialsId: 'github-repo-access', variable: 'accessToken' ) ]) {
                    // NOTE: creating an empty stub directory just to create the release
                    sh(
                        script: """
                            #!/usr/bin/env bash

                            cd "${env.WORKSPACE}"

                            GITHUB_USER=${repoUser} \
                            GITHUB_TOKEN=${accessToken} \
                            python3 ./scripts/release-on-github.py \
                                ${repoName} \
                                \$(mktemp -d) \
                                ${version} \
                                "${changelog}"
                        """
                    )
                }
            }
        }
     }

    post {
        always {
            node('linuxbuild') {
                script {
                    sh 'docker container prune -f && docker volume prune -f && docker image prune -f'
                }
            }
        }

        success {
            node( 'built-in' ) {
                withCredentials([ string( credentialsId: 'wire-jenkinsbot', variable: 'jenkinsbot_secret' ) ]) {
                    sh 'echo noop'
                    //wireSend secret: "$jenkinsbot_secret", message: "✅ ${JOB_NAME} #${BUILD_ID} succeeded\n**Changelog:** ${changelog}\n${BUILD_URL}console\nhttps://${REPO_BASE_PATH}/commit/${commitId}"
                }
            }
        }

        failure {
            node( 'built-in' ) {
                withCredentials([ string( credentialsId: 'wire-jenkinsbot', variable: 'jenkinsbot_secret' ) ]) {
                    sh 'echo noop'
                    //wireSend secret: "$jenkinsbot_secret", message: "❌ ${JOB_NAME} #${BUILD_ID} failed\n${BUILD_URL}console\nhttps://${REPO_BASE_PATH}/commit/${commitId}"
                }
            }
        }
    }
}
